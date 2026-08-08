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

bool hasUsableSecret(const String& value) {
  return value.length() > 0 && value != "YOUR_HUB_SERVER_API_KEY" &&
         value != "YOUR_HUB_DEVICE_SECRET";
}

}  // namespace

HubService::HubService() {
  stateMutex_ = xSemaphoreCreateRecursiveMutex();
}

void HubService::begin(const char* baseUrl, const char* deviceSecret, const char* deviceId) {
  lockState();
  baseUrl_ = baseUrl ? baseUrl : "";
  deviceSecret_ = deviceSecret ? deviceSecret : "";
  deviceId_ = deviceId ? deviceId : "tinypanel-001";

  baseUrl_.trim();
  deviceSecret_.trim();
  while (baseUrl_.endsWith("/")) {
    baseUrl_.remove(baseUrl_.length() - 1);
  }
  unlockState();
}

void HubService::setDeviceSecret(const char* deviceSecret) {
  lockState();
  deviceSecret_ = deviceSecret ? deviceSecret : "";
  deviceSecret_.trim();
  unlockState();
}

void HubService::setDeviceBinding(bool bound, const char* bindCode, const char* name) {
  lockState();
  bound_ = bound;
  bindCode_ = bindCode ? bindCode : "";
  deviceName_ = name ? name : "";
  unlockState();
}

bool HubService::isConfigured() const {
  lockState();
  const bool configured = baseUrl_.length() > 0 && hasUsableCredential(deviceSecret_.c_str());
  unlockState();
  return configured;
}

bool HubService::canHello() const {
  lockState();
  const bool available = baseUrl_.length() > 0;
  unlockState();
  return available;
}

const String& HubService::deviceId() const {
  return deviceId_;
}

bool HubService::isBound() const {
  lockState();
  const bool bound = bound_;
  unlockState();
  return bound;
}

const String& HubService::bindCode() const {
  return bindCode_;
}

const String& HubService::deviceName() const {
  return deviceName_;
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
  lockState();
  const bool syncing = syncState_ == HubSyncState::Syncing;
  unlockState();
  return syncing;
}

bool HubService::hasFailed() const {
  lockState();
  const bool failed = syncState_ == HubSyncState::Failed;
  unlockState();
  return failed;
}

bool HubService::update(uint32_t nowMs) {
  lockState();
  if (syncState_ != HubSyncState::Syncing || !requestResultPending_) {
    unlockState();
    return false;
  }
  if (!timeReached(nowMs, syncMinUntilMs_)) {
    unlockState();
    return false;
  }

  syncState_ = lastRequestOk_ ? HubSyncState::Idle : HubSyncState::Failed;
  requestResultPending_ = false;
  unlockState();
  return true;
}

HubHelloResult HubService::hello(bool networkReady, HubStateChangedCallback onStateChanged, uint32_t nowMs) {
  if (baseUrl_.length() == 0 || !networkReady) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubHelloResult result = sendHello();
  if (result.ok && result.deviceSecret.length() > 0) {
    setDeviceSecret(result.deviceSecret.c_str());
  }
  if (result.ok) {
    lockState();
    bound_ = result.bound;
    bindCode_ = result.bindCode;
    deviceName_ = result.name;
    unlockState();
  }
  completeRequest(result, nowMs);
  return result;
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
  if (result.ok) {
    lastTelemetryMs_ = nowMs;
  }
  completeRequest(result, nowMs);
  return result;
}

HubRequestResult HubService::pollMessages(bool force,
                                          bool networkReady,
                                          HubStateChangedCallback onStateChanged,
                                          uint32_t nowMs,
                                          HubMessagesPersistCallback persist,
                                          void* persistContext) {
  if (!messagePollDue(force, nowMs) || !isConfigured() || !networkReady) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result = syncSubscription(persist, persistContext);
  if (result.ok) {
    lastMessagePollMs_ = nowMs;
  }
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
  if (result.ok) {
    lastWeatherPollMs_ = nowMs;
  }
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
  if (result.ok) {
    lastTodoPollMs_ = nowMs;
  }
  completeRequest(result, nowMs);
  return result;
}

HubRequestResult HubService::syncTodoChanges(bool networkReady,
                                             HubStateChangedCallback onStateChanged,
                                             uint32_t nowMs) {
  if (!isConfigured() || !networkReady) {
    return {};
  }

  HubTodo patches[MaxTodos];
  size_t patchCount = 0;
  HubTodoDelete deletes[MaxTodos];
  size_t deleteCount = 0;
  lockState();
  for (size_t i = 0; i < todoCount_; ++i) {
    if (todos_[i].dirty) {
      patches[patchCount++] = todos_[i];
    }
  }
  deleteCount = pendingTodoDeleteCount_;
  for (size_t i = 0; i < deleteCount; ++i) {
    deletes[i] = pendingTodoDeletes_[i];
  }
  unlockState();
  if (patchCount == 0 && deleteCount == 0) {
    return {};
  }

  beginRequest(nowMs, onStateChanged);
  HubRequestResult result;
  result.attempted = true;
  result.ok = true;
  result.changed = true;

  for (size_t i = 0; i < patchCount; ++i) {
    HubTodo updated;
    HubRequestResult patchResult =
        patchTodoStatus(patches[i].id, patches[i].status, patches[i].version, updated);
    result.statusCode = patchResult.statusCode;
    if (!patchResult.ok) {
      result.ok = false;
      result.retryable = patchResult.retryable || patchResult.statusCode == 409;
      break;
    }

    lockState();
    for (size_t todoIndex = 0; todoIndex < todoCount_; ++todoIndex) {
      HubTodo& current = todos_[todoIndex];
      if (current.id != patches[i].id) {
        continue;
      }
      const bool unchangedLocally = current.status == patches[i].status &&
                                    current.version == patches[i].version;
      current.version = updated.version;
      current.updatedAt = updated.updatedAt;
      current.dirty = !unchangedLocally;
      break;
    }
    for (size_t deleteIndex = 0; deleteIndex < pendingTodoDeleteCount_; ++deleteIndex) {
      if (pendingTodoDeletes_[deleteIndex].id == patches[i].id &&
          pendingTodoDeletes_[deleteIndex].version == patches[i].version) {
        pendingTodoDeletes_[deleteIndex].version = updated.version;
      }
    }
    unlockState();
  }

  if (result.ok) {
    for (size_t i = 0; i < deleteCount; ++i) {
      lockState();
      int currentVersion = deletes[i].version;
      for (size_t pendingIndex = 0; pendingIndex < pendingTodoDeleteCount_; ++pendingIndex) {
        if (pendingTodoDeletes_[pendingIndex].id == deletes[i].id) {
          currentVersion = pendingTodoDeletes_[pendingIndex].version;
          break;
        }
      }
      unlockState();

      HubRequestResult deleteResult = deleteTodoByVersion(deletes[i].id, currentVersion);
      result.statusCode = deleteResult.statusCode;
      if (!deleteResult.ok) {
        result.ok = false;
        result.retryable = deleteResult.retryable || deleteResult.statusCode == 409;
        break;
      }

      lockState();
      for (size_t pendingIndex = 0; pendingIndex < pendingTodoDeleteCount_; ++pendingIndex) {
        if (pendingTodoDeletes_[pendingIndex].id != deletes[i].id) {
          continue;
        }
        for (size_t move = pendingIndex + 1; move < pendingTodoDeleteCount_; ++move) {
          pendingTodoDeletes_[move - 1] = pendingTodoDeletes_[move];
        }
        --pendingTodoDeleteCount_;
        break;
      }
      unlockState();
    }
  }

  if (result.ok || result.statusCode == 409) {
    HubRequestResult refresh = fetchTodos();
    result.ok = result.ok && refresh.ok;
    if (!refresh.ok) {
      result.statusCode = refresh.statusCode;
      result.retryable = result.retryable || refresh.retryable;
    }
  }

  completeRequest(result, nowMs);
  return result;
}

bool HubService::setTodoStatusLocal(size_t index, int status) {
  lockState();
  if (index >= todoCount_ || status < 0 || status > 2) {
    unlockState();
    return false;
  }
  if (todos_[index].status != status) {
    todos_[index].status = status;
    todos_[index].dirty = true;
  }
  unlockState();
  return true;
}

bool HubService::deleteTodoLocal(size_t index) {
  lockState();
  if (index >= todoCount_ || pendingTodoDeleteCount_ >= MaxTodos) {
    unlockState();
    return false;
  }
  if (todos_[index].id > 0 && todos_[index].version > 0) {
    pendingTodoDeletes_[pendingTodoDeleteCount_].id = todos_[index].id;
    pendingTodoDeletes_[pendingTodoDeleteCount_].version = todos_[index].version;
    ++pendingTodoDeleteCount_;
  }
  for (size_t i = index + 1; i < todoCount_; ++i) {
    todos_[i - 1] = todos_[i];
  }
  --todoCount_;
  unlockState();
  return true;
}

bool HubService::hasPendingTodoChanges() const {
  lockState();
  bool pending = pendingTodoDeleteCount_ > 0;
  for (size_t i = 0; i < todoCount_ && !pending; ++i) {
    pending = todos_[i].dirty;
  }
  unlockState();
  return pending;
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

  lockState();
  messageCount_ = count < MaxMessages ? count : MaxMessages;
  for (size_t i = 0; i < messageCount_; ++i) {
    messages_[i] = messages[i];
  }
  messagesDurable_ = true;
  unlockState();
  return true;
}

bool HubService::deleteMessageLocal(size_t index) {
  lockState();
  if (index >= messageCount_) {
    unlockState();
    return false;
  }
  for (size_t i = index + 1; i < messageCount_; ++i) {
    messages_[i - 1] = messages_[i];
  }
  --messageCount_;
  messages_[messageCount_] = {};
  messagesDurable_ = false;
  unlockState();
  return true;
}

void HubService::clearMessagesLocal() {
  lockState();
  for (size_t i = 0; i < messageCount_; ++i) {
    messages_[i] = {};
  }
  messageCount_ = 0;
  messagesDurable_ = false;
  unlockState();
}

void HubService::markMessagesPersisted() {
  lockState();
  messagesDurable_ = true;
  unlockState();
}

const HubWeather& HubService::weather() const {
  return weather_;
}

bool HubService::setWeather(const HubWeather& weather) {
  lockState();
  weather_ = weather;
  const bool valid = weather_.valid;
  unlockState();
  return valid;
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

  lockState();
  todoCount_ = count < MaxTodos ? count : MaxTodos;
  for (size_t i = 0; i < todoCount_; ++i) {
    todos_[i] = todos[i];
  }
  pendingTodoDeleteCount_ = 0;
  unlockState();
  return true;
}

bool HubService::setPendingTodoDeletes(const HubTodoDelete* deletes, size_t count) {
  if (!deletes && count > 0) {
    return false;
  }
  lockState();
  pendingTodoDeleteCount_ = count < MaxTodos ? count : MaxTodos;
  for (size_t i = 0; i < pendingTodoDeleteCount_; ++i) {
    pendingTodoDeletes_[i] = deletes[i];
  }
  unlockState();
  return true;
}

void HubService::snapshot(HubStateSnapshot& out) const {
  lockState();
  out.configured = baseUrl_.length() > 0 && hasUsableCredential(deviceSecret_.c_str());
  out.bound = bound_;
  out.syncing = syncState_ == HubSyncState::Syncing;
  out.failed = syncState_ == HubSyncState::Failed;
  out.deviceId = deviceId_;
  out.bindCode = bindCode_;
  out.deviceName = deviceName_;
  out.messageCount = messageCount_;
  for (size_t i = 0; i < messageCount_; ++i) {
    out.messages[i] = messages_[i];
  }
  out.weather = weather_;
  out.todoCount = todoCount_;
  for (size_t i = 0; i < todoCount_; ++i) {
    out.todos[i] = todos_[i];
  }
  out.pendingTodoDeleteCount = pendingTodoDeleteCount_;
  for (size_t i = 0; i < pendingTodoDeleteCount_; ++i) {
    out.pendingTodoDeletes[i] = pendingTodoDeletes_[i];
  }
  unlockState();
}

HubRequestResult HubService::sendTelemetry(const HubTelemetrySnapshot& snapshot) {
  if (!isConfigured()) {
    return {};
  }

  if (pendingTelemetryBody_.length() == 0) {
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

    pendingTelemetryBody_.reserve(measureJson(doc) + 1);
    serializeJson(doc, pendingTelemetryBody_);
  }

  HubRequestResult result = postJson(AuthMode::Device,
                                     "/device/telemetry",
                                     pendingTelemetryBody_.c_str(),
                                     pendingTelemetryBody_.length(),
                                     nullptr,
                                     "telemetry");
  if (result.ok || !result.retryable) {
    pendingTelemetryBody_ = "";
  }
  return result;
}

HubHelloResult HubService::sendHello() {
  responseDoc_.clear();
  HubRequestResult request =
      postJson(AuthMode::None, "/device/hello", nullptr, 0, &responseDoc_, "device hello");

  HubHelloResult result;
  result.attempted = request.attempted;
  result.ok = request.ok;
  result.statusCode = request.statusCode;
  result.retryable = request.retryable;
  if (!result.ok) {
    return result;
  }

  result.deviceSecret = responseDoc_["device_secret"] | "";
  result.bindCode = responseDoc_["bind_code"] | "";
  result.bindCodeTtlS = responseDoc_["bind_code_ttl"] | 0;
  result.bound = responseDoc_["bound"] | false;
  result.name = responseDoc_["name"] | "";
  result.serverTime = responseDoc_["server_time"] | "";
  return result;
}

HubRequestResult HubService::syncSubscription(HubMessagesPersistCallback persist, void* persistContext) {
  char path[48];
  const int pathLen = snprintf(path, sizeof(path), "/device/messages?limit=%u",
                               static_cast<unsigned>(messageLimit_));
  if (pathLen < 0 || pathLen >= static_cast<int>(sizeof(path))) {
    HubRequestResult result;
    result.attempted = true;
    Serial.println("Hub: messages path too long");
    return result;
  }

  jsonDoc_.clear();
  HubRequestResult result = getJson(AuthMode::Device, path, jsonDoc_, "messages");
  if (!result.ok) {
    return result;
  }

  int ackIds[MaxMessages];
  size_t ackCount = 0;
  bool ok = true;
  JsonArrayConst messages = jsonDoc_["messages"].as<JsonArrayConst>();
  if (messages.isNull()) {
    result.ok = false;
    result.retryable = true;
    return result;
  }

  HubMessage nextMessages[MaxMessages];
  size_t nextCount = 0;
  bool wasDurable = false;
  lockState();
  nextCount = messageCount_;
  for (size_t i = 0; i < messageCount_; ++i) {
    nextMessages[i] = messages_[i];
  }
  wasDurable = messagesDurable_;
  unlockState();

  for (JsonObjectConst item : messages) {
    if (ackCount >= MaxMessages) {
      break;
    }
    HubMessage message;
    if (!parseMessage(item, message)) {
      ok = false;
      continue;
    }
    storeMessageIn(nextMessages, nextCount, message);
    ackIds[ackCount++] = message.id;
  }

  lockState();
  result.changed = !sameMessages(messages_, messageCount_, nextMessages, nextCount);
  unlockState();

  const bool needsPersistence = result.changed || !wasDurable;
  bool persisted = !needsPersistence;
  if (needsPersistence && persist) {
    persisted = persist(nextMessages, nextCount, persistContext);
  }
  result.persisted = persisted;

  lockState();
  if (result.changed) {
    messageCount_ = nextCount;
    for (size_t i = 0; i < nextCount; ++i) {
      messages_[i] = nextMessages[i];
    }
  }
  messagesDurable_ = persisted;
  unlockState();

  if (!persisted) {
    result.ok = false;
    result.retryable = true;
    return result;
  }

  if (ackCount > 0) {
    HubRequestResult ackResult = ackMessages(ackIds, ackCount);
    ok = ok && ackResult.ok;
    if (!ackResult.ok) {
      result.statusCode = ackResult.statusCode;
      result.retryable = ackResult.retryable;
    }
  }

  result.ok = ok;
  return result;
}

HubRequestResult HubService::fetchWeather() {
  jsonDoc_.clear();
  responseDoc_.clear();
  responseDoc_["weather"] = true;
  HubRequestResult result =
      getJsonFiltered(AuthMode::Device, "/device/snapshot?include=weather", jsonDoc_, "device snapshot", responseDoc_);
  if (!result.ok) {
    return result;
  }

  HubWeather weather;
  result.ok = parseWeather(jsonDoc_["weather"], weather);
  result.retryable = !result.ok;
  if (result.ok) {
    lockState();
    weather_ = weather;
    unlockState();
  }
  return result;
}

bool HubService::parseWeather(JsonVariantConst source, HubWeather& weather) const {
  JsonObjectConst root = source.as<JsonObjectConst>();
  if (root.isNull()) {
    root = source.as<JsonObjectConst>();
  }
  if (root.isNull()) {
    return false;
  }

  weather = {};
  weather.location = root["location"] | "";
  weather.condition = root["condition"] | "";
  weather.icon = root["icon"] | "";
  weather.temperature = root["temperature"] | 0;
  weather.humidity = root["humidity"] | 0;
  weather.updatedAt = root["updated_at"] | "";

  JsonArrayConst hourly = root["hourly"].as<JsonArrayConst>();
  for (JsonObjectConst item : hourly) {
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

  JsonArrayConst daily = root["daily"].as<JsonArrayConst>();
  for (JsonObjectConst item : daily) {
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
  return weather.valid;
}

HubRequestResult HubService::fetchTodos() {
  jsonDoc_.clear();
  HubRequestResult result = getJson(AuthMode::Device, "/device/todos", jsonDoc_, "todos");
  if (!result.ok) {
    return result;
  }

  JsonArray items = jsonDoc_.as<JsonArray>();
  if (items.isNull()) {
    result.ok = false;
    result.retryable = true;
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

  lockState();
  size_t mergedCount = 0;
  for (size_t i = 0; i < nextCount; ++i) {
    bool pendingDelete = false;
    for (size_t deleteIndex = 0; deleteIndex < pendingTodoDeleteCount_; ++deleteIndex) {
      if (pendingTodoDeletes_[deleteIndex].id == nextTodos[i].id) {
        pendingTodoDeletes_[deleteIndex].version = nextTodos[i].version;
        pendingDelete = true;
      }
    }
    if (pendingDelete) {
      continue;
    }

    for (size_t currentIndex = 0; currentIndex < todoCount_; ++currentIndex) {
      if (todos_[currentIndex].id == nextTodos[i].id && todos_[currentIndex].dirty) {
        nextTodos[i].status = todos_[currentIndex].status;
        nextTodos[i].dirty = true;
        break;
      }
    }
    todos_[mergedCount++] = nextTodos[i];
  }
  todoCount_ = mergedCount;
  unlockState();
  return result;
}

HubRequestResult HubService::patchTodoStatus(int id, int status, int version, HubTodo& updated) {
  jsonDoc_.clear();
  jsonDoc_["version"] = version;
  jsonDoc_["status"] = status;
  String body;
  body.reserve(measureJson(jsonDoc_) + 1);
  serializeJson(jsonDoc_, body);

  char path[48];
  snprintf(path, sizeof(path), "/device/todos/%d", id);
  responseDoc_.clear();
  HubRequestResult result =
      patchJson(AuthMode::Device, path, body.c_str(), body.length(), &responseDoc_, "todo patch");
  if (result.ok) {
    updated.id = id;
    updated.status = status;
    updated.version = responseDoc_["version"] | version;
    updated.updatedAt = responseDoc_["updated_at"] | "";
    result.ok = updated.version > version;
    result.retryable = !result.ok;
  }
  return result;
}

HubRequestResult HubService::deleteTodoByVersion(int id, int version) {
  jsonDoc_.clear();
  jsonDoc_["version"] = version;
  String body;
  body.reserve(measureJson(jsonDoc_) + 1);
  serializeJson(jsonDoc_, body);

  char path[48];
  snprintf(path, sizeof(path), "/device/todos/%d", id);
  return deleteJson(AuthMode::Device, path, body.c_str(), body.length(), "todo delete");
}

bool HubService::parseMessage(JsonObjectConst item, HubMessage& out) const {
  out = {};
  out.id = item["id"] | 0;
  out.channel = item["device_id"] | "";
  out.author = item["author_id"] | "anonymous";
  out.body = item["body"] | "";
  out.createdAt = item["created_at"] | "";
  return out.id > 0 && out.body.length() > 0;
}

HubRequestResult HubService::ackMessages(const int* ids, size_t count) {
  if (!ids || count == 0) {
    return {};
  }

  char body[160];
  int bodyLen = snprintf(body, sizeof(body), "{\"message_ids\":[");
  for (size_t i = 0; i < count; ++i) {
    const int written = snprintf(body + bodyLen,
                                 sizeof(body) - bodyLen,
                                 "%s%d",
                                 i == 0 ? "" : ",",
                                 ids[i]);
    if (written < 0 || bodyLen + written >= static_cast<int>(sizeof(body))) {
      bodyLen = -1;
      break;
    }
    bodyLen += written;
  }
  if (bodyLen >= 0) {
    const int written = snprintf(body + bodyLen, sizeof(body) - bodyLen, "]}");
    if (written < 0 || bodyLen + written >= static_cast<int>(sizeof(body))) {
      bodyLen = -1;
    } else {
      bodyLen += written;
    }
  }

  if (bodyLen < 0) {
    HubRequestResult result;
    result.attempted = true;
    return result;
  }
  return postJson(AuthMode::Device, "/device/messages/ack", body, static_cast<size_t>(bodyLen), nullptr, "message ack");
}

void HubService::storeMessage(const HubMessage& message) {
  lockState();
  storeMessageIn(messages_, messageCount_, message);
  messagesDurable_ = false;
  unlockState();
}

void HubService::storeMessageIn(HubMessage* messages, size_t& count, const HubMessage& message) const {
  for (size_t i = 0; i < count; ++i) {
    if (messages[i].id == message.id) {
      return;
    }
  }

  const size_t insert = count < MaxMessages ? count++ : MaxMessages - 1;
  for (size_t i = insert; i > 0; --i) {
    messages[i] = messages[i - 1];
  }
  messages[0] = message;
}

bool HubService::sameMessages(const HubMessage* left,
                              size_t leftCount,
                              const HubMessage* right,
                              size_t rightCount) const {
  if (leftCount != rightCount) {
    return false;
  }
  for (size_t i = 0; i < leftCount; ++i) {
    if (left[i].id != right[i].id || left[i].channel != right[i].channel ||
        left[i].author != right[i].author || left[i].body != right[i].body ||
        left[i].createdAt != right[i].createdAt) {
      return false;
    }
  }
  return true;
}

bool HubService::hasMessage(int id) const {
  for (size_t i = 0; i < messageCount_; ++i) {
    if (messages_[i].id == id) {
      return true;
    }
  }
  return false;
}

HubRequestResult HubService::postJson(AuthMode auth,
                                      const char* path,
                                      const char* body,
                                      size_t bodyLen,
                                      JsonDocument* response,
                                      const char* label) {
  return requestJson("POST", auth, path, body, bodyLen, response, label);
}

HubRequestResult HubService::getJson(AuthMode auth, const char* path, JsonDocument& doc, const char* label) {
  return requestJson("GET", auth, path, nullptr, 0, &doc, label);
}

HubRequestResult HubService::getJsonFiltered(AuthMode auth,
                                             const char* path,
                                             JsonDocument& doc,
                                             const char* label,
                                             JsonDocument& filter) {
  return requestJson("GET", auth, path, nullptr, 0, &doc, label, &filter);
}

HubRequestResult HubService::patchJson(AuthMode auth,
                                       const char* path,
                                       const char* body,
                                       size_t bodyLen,
                                       JsonDocument* response,
                                       const char* label) {
  return requestJson("PATCH", auth, path, body, bodyLen, response, label);
}

HubRequestResult HubService::deleteJson(AuthMode auth,
                                        const char* path,
                                        const char* body,
                                        size_t bodyLen,
                                        const char* label) {
  return requestJson("DELETE", auth, path, body, bodyLen, nullptr, label);
}

HubRequestResult HubService::requestJson(const char* method,
                                         AuthMode auth,
                                         const char* path,
                                         const char* body,
                                         size_t bodyLen,
                                         JsonDocument* response,
                                         const char* label,
                                         JsonDocument* filter) {
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
    result.retryable = true;
    return result;
  }

  if (auth == AuthMode::Device) {
    http.addHeader("X-Device-ID", deviceId_);
    http.addHeader("X-Device-Secret", deviceSecret_);
  } else {
    http.addHeader("X-Device-ID", deviceId_);
    if (hasUsableSecret(deviceSecret_)) {
      http.addHeader("X-Device-Secret", deviceSecret_);
    }
  }
  if (body) {
    http.addHeader("Content-Type", "application/json");
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Connection", "close");

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
  result.retryable = result.statusCode < 0 || result.statusCode == 401 ||
                     result.statusCode == 403 || result.statusCode == 408 ||
                     result.statusCode == 429 || result.statusCode >= 500;
  String responseBody;
  bool responseBodyLoaded = false;
  if (result.ok && response) {
    const int contentLength = http.getSize();
    if (contentLength > 0) {
      responseBody.reserve(static_cast<size_t>(contentLength) + 1);
    }
    responseBody = http.getString();
    responseBodyLoaded = true;
    DeserializationError error = filter ? deserializeJson(*response, responseBody, DeserializationOption::Filter(*filter))
                                        : deserializeJson(*response, responseBody);
    result.ok = !error;
    if (error) {
      result.retryable = true;
      Serial.printf("Hub: %s JSON failed (%s, %u bytes)\n",
                    label,
                    error.c_str(),
                    static_cast<unsigned>(responseBody.length()));
    }
  }
  Serial.printf("Hub: %s %s %s (%d)\n", label, method, result.ok ? "ok" : "failed", result.statusCode);
  if (verbose_ && !result.ok) {
    if (!responseBodyLoaded) {
      responseBody = http.getString();
    }
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
  lockState();
  syncState_ = HubSyncState::Syncing;
  syncMinUntilMs_ = nowMs + syncIconMinMs_;
  requestResultPending_ = false;
  lastRequestOk_ = true;
  unlockState();

  if (onStateChanged) {
    onStateChanged();
  }
}

void HubService::completeRequest(const HubRequestResult& result, uint32_t nowMs) {
  (void)nowMs;
  lockState();
  lastRequestOk_ = result.ok;
  requestResultPending_ = true;
  unlockState();
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

void HubService::lockState() const {
  if (stateMutex_) {
    xSemaphoreTakeRecursive(stateMutex_, portMAX_DELAY);
  }
}

void HubService::unlockState() const {
  if (stateMutex_) {
    xSemaphoreGiveRecursive(stateMutex_);
  }
}
