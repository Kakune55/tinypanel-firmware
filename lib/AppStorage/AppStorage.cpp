#include "AppStorage.h"

#include <ArduinoJson.h>

namespace {

constexpr uint8_t kSchemaVersion = 1;

void copyField(char* dest, size_t destSize, const char* value) {
  if (!dest || destSize == 0) {
    return;
  }
  snprintf(dest, destSize, "%s", value ? value : "");
}

}  // namespace

bool AppStorage::begin(SdCardStorage& sd) {
  sd_ = &sd;
  ready_ = sd_->isMounted() && sd_->ensureDir(RootDir) && sd_->ensureDir(ConfigDir) &&
           sd_->ensureDir(CacheDir) && sd_->ensureDir(LogsDir) && sd_->ensureDir(CalibDir) &&
           sd_->ensureDir(StateDir);
  return ready_;
}

bool AppStorage::isReady() const {
  return ready_ && sd_ && sd_->isMounted();
}

bool AppStorage::loadWifiCredentials(StoredWifiCredentials& out) const {
  out.count = 0;
  if (!isReady()) {
    return false;
  }

  String text;
  if (!sd_->readText(WifiPath, text, 4096)) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, text);
  if (error) {
    return false;
  }

  JsonArray networks = doc["networks"].as<JsonArray>();
  if (networks.isNull()) {
    return false;
  }

  for (JsonObject network : networks) {
    if (out.count >= StoredWifiCredentials::MaxNetworks) {
      break;
    }
    const char* ssid = network["ssid"] | "";
    const char* password = network["password"] | "";
    if (!ssid[0]) {
      continue;
    }

    const size_t index = out.count++;
    copyField(out.ssids[index], sizeof(out.ssids[index]), ssid);
    copyField(out.passwords[index], sizeof(out.passwords[index]), password);
    out.credentials[index] = {out.ssids[index], out.passwords[index]};
  }

  return out.count > 0;
}

bool AppStorage::saveMessages(const HubMessage* messages, size_t count) {
  if (!isReady() || (!messages && count > 0)) {
    return false;
  }

  JsonDocument doc;
  doc["version"] = kSchemaVersion;
  JsonArray items = doc["messages"].to<JsonArray>();
  const size_t limit = count < HubService::MaxMessages ? count : HubService::MaxMessages;
  for (size_t i = 0; i < limit; ++i) {
    JsonObject item = items.add<JsonObject>();
    item["id"] = messages[i].id;
    item["channel"] = messages[i].channel;
    item["author"] = messages[i].author;
    item["body"] = messages[i].body;
    item["created_at"] = messages[i].createdAt;
  }

  String text;
  serializeJson(doc, text);
  return sd_->writeText(MessagesPath, text);
}

bool AppStorage::loadMessages(HubMessage* out, size_t maxCount, size_t& outCount) const {
  outCount = 0;
  if (!isReady() || !out || maxCount == 0) {
    return false;
  }

  String text;
  if (!sd_->readText(MessagesPath, text, 8192)) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, text);
  if (error) {
    return false;
  }

  JsonArray messages = doc["messages"].as<JsonArray>();
  if (messages.isNull()) {
    return false;
  }

  for (JsonObject item : messages) {
    if (outCount >= maxCount) {
      break;
    }
    HubMessage& message = out[outCount];
    message.id = item["id"] | 0;
    message.channel = item["channel"] | "";
    message.author = item["author"] | "";
    message.body = item["body"] | "";
    message.createdAt = item["created_at"] | "";
    if (message.id > 0 && message.body.length() > 0) {
      ++outCount;
    }
  }

  return outCount > 0;
}

bool AppStorage::loadBatteryCurve(BatteryCurvePoint* out, size_t maxCount, size_t& outCount) const {
  outCount = 0;
  if (!isReady() || !out || maxCount < 2) {
    return false;
  }

  String text;
  if (!sd_->readText(BatteryCurvePath, text, 8192)) {
    return false;
  }

  int start = 0;
  while (start < static_cast<int>(text.length()) && outCount < maxCount) {
    int end = text.indexOf('\n', start);
    if (end < 0) {
      end = text.length();
    }

    String line = text.substring(start, end);
    line.trim();
    BatteryCurvePoint point;
    if (parseBatteryCurveLine(line, point)) {
      out[outCount++] = point;
    }
    start = end + 1;
  }

  return outCount >= 2;
}

bool AppStorage::appendBatterySample(const BatteryStatus& battery, const RtcDateTime& now, uint32_t uptimeS) {
  if (!isReady()) {
    return false;
  }

  const String path = batteryLogPath(now);
  if (!sd_->exists(path.c_str())) {
    sd_->appendLine(path.c_str(), "timestamp,uptime_s,raw_adc,raw_voltage_mv,voltage,percent,charging");
  }

  String line = timestampOrUptime(now, uptimeS);
  line += ",";
  line += String(uptimeS);
  line += ",";
  line += String(battery.rawAdc);
  line += ",";
  line += String(battery.rawVoltageMv);
  line += ",";
  line += String(battery.voltage, 3);
  line += ",";
  line += String(battery.percentFloat, 2);
  line += ",";
  line += battery.charging ? "1" : "0";
  return sd_->appendLine(path.c_str(), line);
}

String AppStorage::batteryLogPath(const RtcDateTime& now) const {
  char buffer[48];
  if (now.valid) {
    snprintf(buffer, sizeof(buffer), "/tinypanel/logs/battery_%04u%02u%02u.csv", now.year, now.month, now.day);
  } else {
    snprintf(buffer, sizeof(buffer), "/tinypanel/logs/battery_uptime.csv");
  }
  return String(buffer);
}

String AppStorage::timestampOrUptime(const RtcDateTime& now, uint32_t uptimeS) const {
  if (!now.valid) {
    return String("uptime_") + String(uptimeS);
  }

  char buffer[32];
  snprintf(buffer,
           sizeof(buffer),
           "%04u-%02u-%02uT%02u:%02u:%02u+08:00",
           now.year,
           now.month,
           now.day,
           now.hour,
           now.minute,
           now.second);
  return String(buffer);
}

bool AppStorage::parseBatteryCurveLine(const String& line, BatteryCurvePoint& out) const {
  if (line.length() == 0 || line[0] == '#') {
    return false;
  }

  const int comma = line.indexOf(',');
  if (comma <= 0) {
    return false;
  }

  String rawText = line.substring(0, comma);
  String percentText = line.substring(comma + 1);
  rawText.trim();
  percentText.trim();
  if (!isDigit(rawText[0]) || (!isDigit(percentText[0]) && percentText[0] != '.')) {
    return false;
  }

  out.rawAdc = rawText.toInt();
  out.percent = percentText.toFloat();
  return out.rawAdc > 0 && out.percent >= 0.0f && out.percent <= 100.0f;
}
