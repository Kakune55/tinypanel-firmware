#include "HubService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <cstring>

namespace {

constexpr uint8_t kSchemaVersion = 1;
constexpr uint32_t kHttpTimeoutMs = 3000;

uint32_t toMillivolts(float voltage) {
  return static_cast<uint32_t>(voltage * 1000.0f + 0.5f);
}

const char* batteryStatusText(const BatteryStatus& battery, bool usbConnected) {
  if (usbConnected) {
    return battery.percentFloat >= 98.0f ? "full" : "charging";
  }
  return battery.critical ? "critical" : "discharging";
}

}  // namespace

void HubService::begin(const char* baseUrl, const char* apiKey, const char* deviceId) {
  baseUrl_ = baseUrl ? baseUrl : "";
  apiKey_ = apiKey ? apiKey : "";
  deviceId_ = deviceId ? deviceId : "tinypanel-001";

  baseUrl_.trim();
  while (baseUrl_.endsWith("/")) {
    baseUrl_.remove(baseUrl_.length() - 1);
  }
}

bool HubService::isConfigured() const {
  return baseUrl_.length() > 0 && hasUsableCredential(apiKey_.c_str());
}

void HubService::configureTelemetry(uint32_t intervalMs, uint32_t syncIconMinMs) {
  telemetryIntervalMs_ = intervalMs;
  syncIconMinMs_ = syncIconMinMs;
}

void HubService::configureMessages(const char* channel, uint32_t pollIntervalMs, uint8_t limit) {
  messageChannel_ = channel && channel[0] ? channel : "desk";
  messagePollIntervalMs_ = pollIntervalMs;
  messageLimit_ = static_cast<uint8_t>(min(static_cast<int>(limit), static_cast<int>(MaxMessages)));
  if (messageLimit_ == 0) {
    messageLimit_ = 1;
  }
}

void HubService::configureWeather(uint32_t pollIntervalMs) {
  weatherPollIntervalMs_ = pollIntervalMs;
}

void HubService::configureTodos(uint32_t pollIntervalMs, uint8_t limit) {
  todoPollIntervalMs_ = pollIntervalMs;
  todoLimit_ = static_cast<uint8_t>(min(static_cast<int>(limit), static_cast<int>(MaxTodos)));
  if (todoLimit_ == 0) {
    todoLimit_ = 1;
  }
}

void HubService::setVerbose(bool verbose) {
  verbose_ = verbose;
}

bool HubService::isSyncing() const {
  return syncState_ == HubSyncState::Syncing;
}

bool HubService::hasFailed() const {
  return syncState_ == HubSyncState::Failed;
}

bool HubService::update(uint32_t nowMs) {
  if (syncState_ != HubSyncState::Syncing || !requestResultPending_) {
    return false;
  }
  if (!timeReached(nowMs, syncMinUntilMs_)) {
    return false;
  }

  syncState_ = lastRequestOk_ ? HubSyncState::Idle : HubSyncState::Failed;
  requestResultPending_ = false;
  return true;
}

HubRequestResult HubService::syncTelemetry(const HubTelemetrySnapshot& snapshot,
                                           bool force,
                                           bool networkReady,
                                           HubStateChangedCallback onStateChanged,
                                           uint32_t nowMs) {
  if (!telemetryDue(force, nowMs) || !isConfigured() || !networkReady) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result = sendTelemetry(snapshot);
  lastTelemetryMs_ = nowMs;
  completeRequest(result, nowMs);
  return result;
}

HubRequestResult HubService::pollMessages(bool force,
                                          bool networkReady,
                                          HubStateChangedCallback onStateChanged,
                                          uint32_t nowMs) {
  if (!messagePollDue(force, nowMs) || !isConfigured() || !networkReady) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result = syncSubscription();
  lastMessagePollMs_ = nowMs;
  completeRequest(result, nowMs);
  return result;
}

HubRequestResult HubService::pollWeather(bool force,
                                         bool networkReady,
                                         HubStateChangedCallback onStateChanged,
                                         uint32_t nowMs) {
  if (!weatherPollDue(force, nowMs) || !isConfigured() || !networkReady) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result = fetchWeather();
  lastWeatherPollMs_ = nowMs;
  completeRequest(result, nowMs);
  return result;
}

HubRequestResult HubService::pollTodos(bool force,
                                       bool networkReady,
                                       HubStateChangedCallback onStateChanged,
                                       uint32_t nowMs) {
  if (!todoPollDue(force, nowMs) || !isConfigured() || !networkReady) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result = fetchTodos();
  lastTodoPollMs_ = nowMs;
  completeRequest(result, nowMs);
  return result;
}

HubRequestResult HubService::syncTodoChanges(bool networkReady,
                                             HubStateChangedCallback onStateChanged,
                                             uint32_t nowMs) {
  if (!isConfigured() || !networkReady) {
    return {};
  }

  bool hasChanges = pendingTodoDeleteCount_ > 0;
  for (size_t i = 0; i < todoCount_ && !hasChanges; ++i) {
    hasChanges = todos_[i].dirty;
  }
  if (!hasChanges) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result;
  result.attempted = true;
  result.ok = true;

  for (size_t i = 0; i < todoCount_; ++i) {
    if (!todos_[i].dirty) {
      continue;
    }
    HubRequestResult patchResult = patchTodoStatus(todos_[i]);
    result.statusCode = patchResult.statusCode;
    result.ok = result.ok && patchResult.ok;
    if (!patchResult.ok && patchResult.statusCode == 409) {
      break;
    }
  }

  if (result.ok) {
    for (size_t i = 0; i < pendingTodoDeleteCount_; ++i) {
      HubRequestResult deleteResult =
          deleteTodoByVersion(pendingTodoDeletes_[i].id, pendingTodoDeletes_[i].version);
      result.statusCode = deleteResult.statusCode;
      result.ok = result.ok && deleteResult.ok;
      if (!deleteResult.ok && deleteResult.statusCode == 409) {
        break;
      }
    }
  }

  if (result.ok) {
    pendingTodoDeleteCount_ = 0;
    lastTodoPollMs_ = 0;
    HubRequestResult refresh = fetchTodos();
    result.ok = refresh.ok;
    result.statusCode = refresh.statusCode;
  } else if (result.statusCode == 409) {
    pendingTodoDeleteCount_ = 0;
    fetchTodos();
  }

  completeRequest(result, nowMs);
  return result;
}

bool HubService::setTodoStatusLocal(size_t index, int status) {
  if (index >= todoCount_ || status < 0 || status > 2) {
    return false;
  }
  if (todos_[index].status == status) {
    return true;
  }
  todos_[index].status = status;
  todos_[index].dirty = true;
  return true;
}

bool HubService::deleteTodoLocal(size_t index) {
  if (index >= todoCount_) {
    return false;
  }
  if (pendingTodoDeleteCount_ < MaxTodos && todos_[index].id > 0 && todos_[index].version > 0) {
    pendingTodoDeletes_[pendingTodoDeleteCount_].id = todos_[index].id;
    pendingTodoDeletes_[pendingTodoDeleteCount_].version = todos_[index].version;
    ++pendingTodoDeleteCount_;
  }

  for (size_t i = index + 1; i < todoCount_; ++i) {
    todos_[i - 1] = todos_[i];
  }
  --todoCount_;
  return true;
}

size_t HubService::messageCount() const {
  return messageCount_;
}

const HubMessage* HubService::messages() const {
  return messageCount_ > 0 ? messages_ : nullptr;
}

const HubMessage* HubService::messageAt(size_t index) const {
  return index < messageCount_ ? &messages_[index] : nullptr;
}

bool HubService::setMessages(const HubMessage* messages, size_t count) {
  if (!messages && count > 0) {
    return false;
  }

  messageCount_ = count < MaxMessages ? count : MaxMessages;
  for (size_t i = 0; i < messageCount_; ++i) {
    messages_[i] = messages[i];
  }
  return true;
}

bool HubService::deleteMessageLocal(size_t index) {
  if (index >= messageCount_) {
    return false;
  }
  for (size_t i = index + 1; i < messageCount_; ++i) {
    messages_[i - 1] = messages_[i];
  }
  --messageCount_;
  messages_[messageCount_] = {};
  return true;
}

void HubService::clearMessagesLocal() {
  for (size_t i = 0; i < messageCount_; ++i) {
    messages_[i] = {};
  }
  messageCount_ = 0;
}

const HubWeather& HubService::weather() const {
  return weather_;
}

bool HubService::setWeather(const HubWeather& weather) {
  weather_ = weather;
  return weather_.valid;
}

size_t HubService::todoCount() const {
  return todoCount_;
}

const HubTodo* HubService::todos() const {
  return todoCount_ > 0 ? todos_ : nullptr;
}

const HubTodo* HubService::todoAt(size_t index) const {
  return index < todoCount_ ? &todos_[index] : nullptr;
}

bool HubService::setTodos(const HubTodo* todos, size_t count) {
  if (!todos && count > 0) {
    return false;
  }

  todoCount_ = count < MaxTodos ? count : MaxTodos;
  for (size_t i = 0; i < todoCount_; ++i) {
    todos_[i] = todos[i];
    todos_[i].dirty = false;
  }
  pendingTodoDeleteCount_ = 0;
  return true;
}

HubRequestResult HubService::sendTelemetry(const HubTelemetrySnapshot& snapshot) {
  if (!isConfigured()) {
    return {};
  }

  jsonDoc_.clear();
  JsonDocument& doc = jsonDoc_;
  doc["schema_version"] = kSchemaVersion;
  doc["device_id"] = snapshot.deviceId && snapshot.deviceId[0] ? snapshot.deviceId : deviceId_.c_str();
  doc["boot_id"] = snapshot.bootId;
  doc["sequence"] = ++sequence_;
  doc["report_timestamp"] = snapshot.reportTimestamp;
  doc["uptime_s"] = snapshot.uptimeS;

  JsonObject power = doc["power"].to<JsonObject>();
  JsonObject battery = power["battery"].to<JsonObject>();
  battery["raw_adc"] = snapshot.battery.rawAdc;
  battery["raw_voltage_mv"] = snapshot.battery.rawVoltageMv;
  battery["voltage_mv"] = toMillivolts(snapshot.battery.voltage);
  battery["percentage"] = snapshot.battery.percentFloat;
  battery["status"] = batteryStatusText(snapshot.battery, snapshot.usbConnected);
  power["usb_connected"] = snapshot.usbConnected;

  JsonObject environment = doc["environment"].to<JsonObject>();
  JsonObject shtc3 = environment["shtc3"].to<JsonObject>();
  shtc3["temperature_c"] = snapshot.environment.temperatureC;
  shtc3["humidity_rh"] = snapshot.environment.humidityRh;
  shtc3["sensor_ok"] = snapshot.environment.valid;

  JsonObject network = doc["network"].to<JsonObject>();
  network["wifi_connected"] = snapshot.wifiConnected;
  network["ssid"] = snapshot.wifiSsid;
  network["rssi_dbm"] = snapshot.wifiRssiDbm;
  network["ip"] = snapshot.wifiIp;

  JsonObject system = doc["system"].to<JsonObject>();
  system["free_heap_bytes"] = snapshot.freeHeapBytes;
  system["free_psram_bytes"] = snapshot.freePsramBytes;
  system["ntp_sync"] = snapshot.ntpSync;

  JsonObject storage = doc["storage"].to<JsonObject>();
  storage["sd_card_present"] = snapshot.sdCardPresent;
  storage["sd_card_total_mb"] = snapshot.sdCardTotalMb;
  storage["sd_card_used_mb"] = snapshot.sdCardUsedMb;

  doc["app"].to<JsonObject>();

  String body;
  body.reserve(measureJson(doc) + 1);
  serializeJson(doc, body);

  return postJson("/telemetry", body.c_str(), body.length(), "telemetry");
}

HubRequestResult HubService::syncSubscription() {
  char channel[96];
  char device[96];
  char path[240];
  if (!urlEncode(messageChannel_.c_str(), channel, sizeof(channel)) ||
      !urlEncode(deviceId_.c_str(), device, sizeof(device))) {
    HubRequestResult result;
    result.attempted = true;
    Serial.println("Hub: subscription path too long");
    return result;
  }

  const int pathLen = snprintf(path, sizeof(path), "/subscriptions/%s?device_id=%s&limit=%u",
                               channel, device, static_cast<unsigned>(messageLimit_));
  if (pathLen < 0 || pathLen >= static_cast<int>(sizeof(path))) {
    HubRequestResult result;
    result.attempted = true;
    Serial.println("Hub: subscription path too long");
    return result;
  }

  jsonDoc_.clear();
  HubRequestResult result = getJson(path, jsonDoc_, "subscription");
  if (!result.ok) {
    return result;
  }

  int messageIds[MaxMessages];
  size_t messageIdCount = 0;
  bool ok = true;
  JsonArray ids = jsonDoc_["message_ids"].as<JsonArray>();
  for (JsonVariant idValue : ids) {
    if (messageIdCount >= MaxMessages) {
      break;
    }
    const int id = idValue.as<int>();
    if (id <= 0) {
      continue;
    }
    messageIds[messageIdCount++] = id;
  }
  jsonDoc_.clear();

  for (size_t i = 0; i < messageIdCount; ++i) {
    const int id = messageIds[i];
    HubMessage message;
    HubRequestResult fetchResult = fetchMessage(id, message);
    ok = ok && fetchResult.ok;
    if (!fetchResult.ok) {
      continue;
    }

    storeMessage(message);
    HubRequestResult ackResult = ackMessage(id);
    ok = ok && ackResult.ok;
  }

  result.ok = ok;
  return result;
}

HubRequestResult HubService::fetchWeather() {
  jsonDoc_.clear();
  HubRequestResult result = getJson("/weather", jsonDoc_, "weather");
  if (!result.ok) {
    return result;
  }

  HubWeather weather;
  weather.location = jsonDoc_["location"] | "";
  weather.condition = jsonDoc_["condition"] | "";
  weather.icon = jsonDoc_["icon"] | "";
  weather.temperature = jsonDoc_["temperature"] | 0;
  weather.humidity = jsonDoc_["humidity"] | 0;
  weather.updatedAt = jsonDoc_["updated_at"] | "";

  JsonArray hourly = jsonDoc_["hourly"].as<JsonArray>();
  for (JsonObject item : hourly) {
    if (weather.hourlyCount >= HubWeather::MaxHourly) {
      break;
    }
    HubWeatherHourly& out = weather.hourly[weather.hourlyCount++];
    out.time = item["time"] | "";
    out.condition = item["condition"] | "";
    out.icon = item["icon"] | "";
    out.temperature = item["temperature"] | 0;
    out.humidity = item["humidity"] | 0;
    out.precipitation = item["precipitation"] | 0.0f;
    out.precipProbability = item["precip_probability"] | -1;
    out.windDirection = item["wind_direction"] | "";
    out.windScale = item["wind_scale"] | "";
    out.windSpeed = item["wind_speed"] | 0;
  }

  JsonArray daily = jsonDoc_["daily"].as<JsonArray>();
  for (JsonObject item : daily) {
    if (weather.dailyCount >= HubWeather::MaxDaily) {
      break;
    }
    HubWeatherDaily& out = weather.daily[weather.dailyCount++];
    out.date = item["date"] | "";
    out.sunrise = item["sunrise"] | "";
    out.sunset = item["sunset"] | "";
    out.conditionDay = item["condition_day"] | "";
    out.conditionNight = item["condition_night"] | "";
    out.iconDay = item["icon_day"] | "";
    out.iconNight = item["icon_night"] | "";
    out.temperatureMin = item["temperature_min"] | 0;
    out.temperatureMax = item["temperature_max"] | 0;
    out.humidity = item["humidity"] | 0;
    out.precipitation = item["precipitation"] | 0.0f;
    out.precipProbability = item["precip_probability"] | -1;
    out.windDirectionDay = item["wind_direction_day"] | "";
    out.windScaleDay = item["wind_scale_day"] | "";
    out.windSpeedDay = item["wind_speed_day"] | 0;
    out.windDirectionNight = item["wind_direction_night"] | "";
    out.windScaleNight = item["wind_scale_night"] | "";
    out.windSpeedNight = item["wind_speed_night"] | 0;
  }

  weather.valid = weather.condition.length() > 0 || weather.hourlyCount > 0 || weather.dailyCount > 0;
  result.ok = weather.valid;
  if (result.ok) {
    weather_ = weather;
  }
  return result;
}

HubRequestResult HubService::fetchTodos() {
  jsonDoc_.clear();
  HubRequestResult result = getJson("/todos", jsonDoc_, "todos");
  if (!result.ok) {
    return result;
  }

  JsonArray items = jsonDoc_.as<JsonArray>();
  if (items.isNull()) {
    result.ok = false;
    return result;
  }

  HubTodo nextTodos[MaxTodos];
  size_t nextCount = 0;
  for (JsonObject item : items) {
    if (nextCount >= todoLimit_) {
      break;
    }
    HubTodo& out = nextTodos[nextCount];
    out.id = item["id"] | 0;
    out.text = item["text"] | "";
    out.status = item["status"] | 0;
    out.version = item["version"] | 0;
    out.createdAt = item["created_at"] | "";
    out.updatedAt = item["updated_at"] | "";
    out.dirty = false;
    if (out.id > 0 && out.text.length() > 0 && out.version > 0) {
      ++nextCount;
    }
  }

  for (size_t i = 0; i < nextCount; ++i) {
    todos_[i] = nextTodos[i];
  }
  todoCount_ = nextCount;
  return result;
}

HubRequestResult HubService::patchTodoStatus(HubTodo& todo) {
  char path[32];
  char body[48];
  snprintf(path, sizeof(path), "/todos/%d", todo.id);
  const int bodyLen = snprintf(body,
                               sizeof(body),
                               "{\"version\":%d,\"status\":%d}",
                               todo.version,
                               todo.status);
  if (bodyLen < 0 || bodyLen >= static_cast<int>(sizeof(body))) {
    HubRequestResult result;
    result.attempted = true;
    return result;
  }

  responseDoc_.clear();
  HubRequestResult result = patchJson(path, body, static_cast<size_t>(bodyLen), &responseDoc_, "todo patch");
  if (result.ok) {
    todo.version = responseDoc_["version"] | todo.version;
    todo.updatedAt = responseDoc_["updated_at"] | todo.updatedAt;
    todo.dirty = false;
  }
  return result;
}

HubRequestResult HubService::deleteTodoByVersion(int id, int version) {
  char path[32];
  char body[32];
  snprintf(path, sizeof(path), "/todos/%d", id);
  const int bodyLen = snprintf(body, sizeof(body), "{\"version\":%d}", version);
  if (bodyLen < 0 || bodyLen >= static_cast<int>(sizeof(body))) {
    HubRequestResult result;
    result.attempted = true;
    return result;
  }
  return deleteJson(path, body, static_cast<size_t>(bodyLen), "todo delete");
}

HubRequestResult HubService::fetchMessage(int id, HubMessage& out) {
  char path[32];
  snprintf(path, sizeof(path), "/messages/%d", id);
  jsonDoc_.clear();
  HubRequestResult result = getJson(path, jsonDoc_, "message");
  if (!result.ok) {
    return result;
  }

  out.id = jsonDoc_["id"] | id;
  out.channel = jsonDoc_["channel"] | "";
  out.author = jsonDoc_["author"] | "anonymous";
  out.body = jsonDoc_["body"] | "";
  out.createdAt = jsonDoc_["created_at"] | "";
  result.ok = out.id > 0 && out.body.length() > 0;
  return result;
}

HubRequestResult HubService::ackMessage(int id) {
  char path[40];
  char body[96];
  snprintf(path, sizeof(path), "/messages/%d/ack", id);
  const int bodyLen = snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", deviceId_.c_str());
  if (bodyLen < 0 || bodyLen >= static_cast<int>(sizeof(body))) {
    HubRequestResult result;
    result.attempted = true;
    return result;
  }
  return postJson(path, body, static_cast<size_t>(bodyLen), "message ack");
}

void HubService::storeMessage(const HubMessage& message) {
  if (hasMessage(message.id)) {
    return;
  }

  const size_t insert = messageCount_ < MaxMessages ? messageCount_++ : MaxMessages - 1;
  for (size_t i = insert; i > 0; --i) {
    messages_[i] = messages_[i - 1];
  }
  messages_[0] = message;
}

bool HubService::hasMessage(int id) const {
  for (size_t i = 0; i < messageCount_; ++i) {
    if (messages_[i].id == id) {
      return true;
    }
  }
  return false;
}

HubRequestResult HubService::postJson(const char* path, const char* body, size_t bodyLen, const char* label) {
  return requestJson("POST", path, body, bodyLen, nullptr, label);
}

HubRequestResult HubService::patchJson(const char* path,
                                       const char* body,
                                       size_t bodyLen,
                                       JsonDocument* response,
                                       const char* label) {
  return requestJson("PATCH", path, body, bodyLen, response, label);
}

HubRequestResult HubService::deleteJson(const char* path, const char* body, size_t bodyLen, const char* label) {
  return requestJson("DELETE", path, body, bodyLen, nullptr, label);
}

HubRequestResult HubService::getJson(const char* path, JsonDocument& doc, const char* label) {
  return requestJson("GET", path, nullptr, 0, &doc, label);
}

HubRequestResult HubService::requestJson(const char* method,
                                         const char* path,
                                         const char* body,
                                         size_t bodyLen,
                                         JsonDocument* response,
                                         const char* label) {
  HubRequestResult result;
  result.attempted = true;
  WiFiClient* requestClient = &client_;
  if (baseUrl_.startsWith("https://")) {
    secureClient_.setInsecure();
    requestClient = &secureClient_;
  }

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);

  String url;
  url.reserve(baseUrl_.length() + strlen(path) + 1);
  url = baseUrl_;
  url += path;
  if (!http.begin(*requestClient, url)) {
    Serial.printf("Hub: %s HTTP begin failed\n", label);
    return result;
  }

  String auth;
  auth.reserve(apiKey_.length() + 8);
  auth = "Bearer ";
  auth += apiKey_;
  http.addHeader("Authorization", auth);
  if (body) {
    http.addHeader("Content-Type", "application/json");
  }

  if (strcmp(method, "POST") == 0) {
    result.statusCode = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body ? body : "{}")),
                                  body ? bodyLen : 2);
  } else if (strcmp(method, "PATCH") == 0 || strcmp(method, "DELETE") == 0) {
    result.statusCode = http.sendRequest(method,
                                         reinterpret_cast<uint8_t*>(const_cast<char*>(body ? body : "{}")),
                                         body ? bodyLen : 2);
  } else {
    result.statusCode = http.GET();
  }
  result.ok = result.statusCode >= 200 && result.statusCode < 300;
  if (result.ok && response) {
    DeserializationError error = deserializeJson(*response, http.getStream());
    result.ok = !error;
    if (error) {
      Serial.printf("Hub: %s JSON failed (%s)\n", label, error.c_str());
    }
  }
  Serial.printf("Hub: %s %s %s (%d)\n", label, method, result.ok ? "ok" : "failed", result.statusCode);
  if (verbose_ && !result.ok) {
    const String responseBody = http.getString();
    Serial.printf("Hub: %s response: %.160s\n", label, responseBody.c_str());
  }
  http.end();
  return result;
}

bool HubService::telemetryDue(bool force, uint32_t nowMs) const {
  return force || lastTelemetryMs_ == 0 || nowMs - lastTelemetryMs_ >= telemetryIntervalMs_;
}

bool HubService::messagePollDue(bool force, uint32_t nowMs) const {
  return force || lastMessagePollMs_ == 0 || nowMs - lastMessagePollMs_ >= messagePollIntervalMs_;
}

bool HubService::weatherPollDue(bool force, uint32_t nowMs) const {
  return force || lastWeatherPollMs_ == 0 || nowMs - lastWeatherPollMs_ >= weatherPollIntervalMs_;
}

bool HubService::todoPollDue(bool force, uint32_t nowMs) const {
  return force || lastTodoPollMs_ == 0 || nowMs - lastTodoPollMs_ >= todoPollIntervalMs_;
}

void HubService::beginRequest(uint32_t nowMs, HubStateChangedCallback onStateChanged) {
  syncState_ = HubSyncState::Syncing;
  syncMinUntilMs_ = nowMs + syncIconMinMs_;
  requestResultPending_ = false;
  lastRequestOk_ = true;

  if (onStateChanged) {
    onStateChanged();
  }
}

void HubService::completeRequest(const HubRequestResult& result, uint32_t nowMs) {
  (void)nowMs;
  lastRequestOk_ = result.ok;
  requestResultPending_ = true;
}

bool HubService::urlEncode(const char* value, char* out, size_t outSize) const {
  if (!value || !out || outSize == 0) {
    return false;
  }

  size_t outLen = 0;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; value[i] != '\0'; ++i) {
    const char c = value[i];
    const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      if (outLen + 1 >= outSize) {
        return false;
      }
      out[outLen++] = c;
    } else {
      if (outLen + 3 >= outSize) {
        return false;
      }
      out[outLen++] = '%';
      out[outLen++] = hex[(static_cast<uint8_t>(c) >> 4) & 0x0F];
      out[outLen++] = hex[static_cast<uint8_t>(c) & 0x0F];
    }
  }
  out[outLen] = '\0';
  return true;
}

bool HubService::timeReached(uint32_t nowMs, uint32_t targetMs) const {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

bool HubService::hasUsableCredential(const char* value) const {
  return value && value[0] != '\0' && strncmp(value, "YOUR_", 5) != 0;
}
