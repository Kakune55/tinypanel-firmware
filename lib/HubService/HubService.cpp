#include "HubService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <cstring>

namespace {

constexpr uint8_t kSchemaVersion = 1;
constexpr uint32_t kHttpTimeoutMs = 3000;

uint32_t toMillivolts(float voltage) {
  return static_cast<uint32_t>(voltage * 1000.0f + 0.5f);
}

const char* batteryStatusText(const BatteryStatus& battery, bool usbConnected) {
  if (usbConnected) {
    return battery.percent >= 98 ? "full" : "charging";
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

size_t HubService::messageCount() const {
  return messageCount_;
}

const HubMessage* HubService::messages() const {
  return messageCount_ > 0 ? messages_ : nullptr;
}

const HubMessage* HubService::messageAt(size_t index) const {
  return index < messageCount_ ? &messages_[index] : nullptr;
}

HubRequestResult HubService::sendTelemetry(const HubTelemetrySnapshot& snapshot) {
  if (!isConfigured()) {
    return {};
  }

  JsonDocument doc;
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
  battery["percentage"] = snapshot.battery.percent;
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
  serializeJson(doc, body);

  return postJson("/telemetry", body, "telemetry");
}

HubRequestResult HubService::syncSubscription() {
  JsonDocument doc;
  const String path = String("/subscriptions/") + urlEncode(messageChannel_) +
                      "?device_id=" + urlEncode(deviceId_) +
                      "&limit=" + String(messageLimit_);
  HubRequestResult result = getJson(path.c_str(), doc, "subscription");
  if (!result.ok) {
    return result;
  }

  bool ok = true;
  JsonArray ids = doc["message_ids"].as<JsonArray>();
  for (JsonVariant idValue : ids) {
    const int id = idValue.as<int>();
    if (id <= 0) {
      continue;
    }

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

HubRequestResult HubService::fetchMessage(int id, HubMessage& out) {
  JsonDocument doc;
  HubRequestResult result = getJson((String("/messages/") + id).c_str(), doc, "message");
  if (!result.ok) {
    return result;
  }

  out.id = doc["id"] | id;
  out.channel = doc["channel"] | "";
  out.author = doc["author"] | "anonymous";
  out.body = doc["body"] | "";
  out.createdAt = doc["created_at"] | "";
  result.ok = out.id > 0 && out.body.length() > 0;
  return result;
}

HubRequestResult HubService::ackMessage(int id) {
  JsonDocument doc;
  doc["device_id"] = deviceId_;

  String body;
  serializeJson(doc, body);
  return postJson((String("/messages/") + id + "/ack").c_str(), body, "message ack");
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

HubRequestResult HubService::postJson(const char* path, const String& body, const char* label) {
  return requestJson("POST", path, &body, nullptr, label);
}

HubRequestResult HubService::getJson(const char* path, JsonDocument& doc, const char* label) {
  return requestJson("GET", path, nullptr, &doc, label);
}

HubRequestResult HubService::requestJson(const char* method,
                                         const char* path,
                                         const String* body,
                                         JsonDocument* response,
                                         const char* label) {
  HubRequestResult result;
  result.attempted = true;
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);

  if (!http.begin(client, baseUrl_ + path)) {
    Serial.printf("Hub: %s HTTP begin failed\n", label);
    return result;
  }

  http.addHeader("Authorization", String("Bearer ") + apiKey_);
  if (body) {
    http.addHeader("Content-Type", "application/json");
  }

  if (strcmp(method, "POST") == 0) {
    result.statusCode = http.POST(body ? *body : String("{}"));
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
  http.end();
  return result;
}

bool HubService::telemetryDue(bool force, uint32_t nowMs) const {
  return force || lastTelemetryMs_ == 0 || nowMs - lastTelemetryMs_ >= telemetryIntervalMs_;
}

bool HubService::messagePollDue(bool force, uint32_t nowMs) const {
  return force || lastMessagePollMs_ == 0 || nowMs - lastMessagePollMs_ >= messagePollIntervalMs_;
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

String HubService::urlEncode(const String& value) const {
  String encoded;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(static_cast<uint8_t>(c) >> 4) & 0x0F];
      encoded += hex[static_cast<uint8_t>(c) & 0x0F];
    }
  }
  return encoded;
}

bool HubService::timeReached(uint32_t nowMs, uint32_t targetMs) const {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

bool HubService::hasUsableCredential(const char* value) const {
  return value && value[0] != '\0' && strncmp(value, "YOUR_", 5) != 0;
}
