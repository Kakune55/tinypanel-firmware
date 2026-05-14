#include "AppStorage.h"

#include <ArduinoJson.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr uint8_t kSchemaVersion = 1;
constexpr size_t kMaxWeatherCacheBytes = 24 * 1024;
constexpr size_t kMaxTodoCacheBytes = 16 * 1024;
constexpr uint8_t kMessageCacheVersion = 1;
constexpr uint8_t kMessageIndexHeaderSize = 7;
constexpr uint8_t kMessageRecordHeaderSize = 19;
constexpr size_t kMaxMessageRecordBytes = 64 * 1024;
constexpr char kMessageIndexMagic[] = {'T', 'P', 'M', 'I'};
constexpr char kMessageRecordMagic[] = {'T', 'P', 'M', 'G'};

void copyField(char* dest, size_t destSize, const char* value) {
  if (!dest || destSize == 0) {
    return;
  }
  snprintf(dest, destSize, "%s", value ? value : "");
}

void writeU16(uint8_t* data, size_t& offset, uint16_t value) {
  data[offset++] = static_cast<uint8_t>(value & 0xFF);
  data[offset++] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* data, size_t& offset, uint32_t value) {
  data[offset++] = static_cast<uint8_t>(value & 0xFF);
  data[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
  data[offset++] = static_cast<uint8_t>((value >> 16) & 0xFF);
  data[offset++] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool readU16(const uint8_t* data, size_t len, size_t& offset, uint16_t& out) {
  if (offset + 2 > len) {
    return false;
  }
  out = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
  offset += 2;
  return true;
}

bool readU32(const uint8_t* data, size_t len, size_t& offset, uint32_t& out) {
  if (offset + 4 > len) {
    return false;
  }
  out = static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1]) << 8) |
        (static_cast<uint32_t>(data[offset + 2]) << 16) |
        (static_cast<uint32_t>(data[offset + 3]) << 24);
  offset += 4;
  return true;
}

uint8_t* allocMessageBuffer(size_t len) {
  uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) {
    buffer = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_8BIT));
  }
  return buffer;
}

}  // namespace

bool AppStorage::begin(SdCardStorage& sd) {
  sd_ = &sd;
  ready_ = sd_->isMounted() && sd_->ensureDir(RootDir) && sd_->ensureDir(ConfigDir) &&
           sd_->ensureDir(CacheDir) && sd_->ensureDir(LogsDir) && sd_->ensureDir(CalibDir) &&
           sd_->ensureDir(StateDir) && sd_->ensureDir(MessagesDir);
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

bool AppStorage::loadDeviceConfig(StoredDeviceConfig& config) const {
  if (!isReady()) {
    return false;
  }

  String text;
  if (!sd_->readText(DevicePath, text, 4096)) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, text);
  if (error) {
    return false;
  }

  copyField(config.deviceId, sizeof(config.deviceId), doc["device_id"] | config.deviceId);
  copyField(config.timezone, sizeof(config.timezone), doc["timezone"] | config.timezone);
  copyField(config.messageChannel, sizeof(config.messageChannel), doc["message_channel"] | config.messageChannel);

  const uint32_t telemetryS = doc["hub_telemetry_s"] | 0;
  const uint32_t messagePollS = doc["hub_message_poll_s"] | 0;
  const uint32_t weatherPollS = doc["hub_weather_poll_s"] | 0;
  const uint32_t todoPollS = doc["hub_todo_poll_s"] | 0;
  const uint32_t batteryLogS = doc["battery_log_interval_s"] | 0;
  if (telemetryS > 0) {
    config.hubTelemetryMs = telemetryS * 1000UL;
  }
  if (messagePollS > 0) {
    config.hubMessagePollMs = messagePollS * 1000UL;
  }
  if (weatherPollS > 0) {
    config.hubWeatherPollMs = weatherPollS * 1000UL;
  }
  if (todoPollS > 0) {
    config.hubTodoPollMs = todoPollS * 1000UL;
  }
  if (batteryLogS > 0) {
    config.batteryLogIntervalMs = batteryLogS * 1000UL;
  }

  const int messageLimit = doc["hub_message_limit"] | config.hubMessageLimit;
  const int todoLimit = doc["hub_todo_limit"] | config.hubTodoLimit;
  config.hubMessageLimit = static_cast<uint8_t>(constrain(messageLimit, 1, static_cast<int>(HubService::MaxMessages)));
  config.hubTodoLimit = static_cast<uint8_t>(constrain(todoLimit, 1, static_cast<int>(HubService::MaxTodos)));
  config.loaded = true;
  return true;
}

bool AppStorage::saveMessages(const HubMessage* messages, size_t count) {
  if (!isReady() || (!messages && count > 0)) {
    return false;
  }

  const size_t limit = count < HubService::MaxMessages ? count : HubService::MaxMessages;
  const size_t indexLen = kMessageIndexHeaderSize + limit * 4;
  uint8_t* index = allocMessageBuffer(indexLen);
  if (!index) {
    return false;
  }

  size_t offset = 0;
  std::memcpy(index + offset, kMessageIndexMagic, sizeof(kMessageIndexMagic));
  offset += sizeof(kMessageIndexMagic);
  index[offset++] = kMessageCacheVersion;
  writeU16(index, offset, static_cast<uint16_t>(limit));

  for (size_t i = 0; i < limit; ++i) {
    if (!saveMessageRecord(messages[i])) {
      heap_caps_free(index);
      return false;
    }
    writeU32(index, offset, static_cast<uint32_t>(messages[i].id));
  }

  const bool ok = sd_->writeBinaryAtomic(MessagesIndexPath, index, indexLen);
  heap_caps_free(index);
  return ok;
}

bool AppStorage::loadMessages(HubMessage* out, size_t maxCount, size_t& outCount) const {
  outCount = 0;
  if (!isReady() || !out || maxCount == 0) {
    return false;
  }

  uint8_t* index = nullptr;
  size_t indexLen = 0;
  if (!sd_->readBinaryBuffer(MessagesIndexPath, index, indexLen, kMessageIndexHeaderSize + HubService::MaxMessages * 4, true)) {
    return false;
  }

  bool ok = false;
  size_t offset = 0;
  if (indexLen >= kMessageIndexHeaderSize &&
      std::memcmp(index, kMessageIndexMagic, sizeof(kMessageIndexMagic)) == 0 &&
      index[4] == kMessageCacheVersion) {
    offset = 5;
    uint16_t count = 0;
    ok = readU16(index, indexLen, offset, count);
    for (uint16_t i = 0; ok && i < count && outCount < maxCount; ++i) {
      uint32_t id = 0;
      ok = readU32(index, indexLen, offset, id);
      if (!ok) {
        break;
      }
      HubMessage message;
      if (loadMessageRecord(static_cast<int>(id), message)) {
        out[outCount++] = message;
      }
    }
  }
  sd_->freeBuffer(index);

  if (outCount > 0) {
    return true;
  }
  return false;
}

bool AppStorage::saveMessageRecord(const HubMessage& message) const {
  if (message.id <= 0) {
    return false;
  }

  const uint16_t createdLen = static_cast<uint16_t>(min(message.createdAt.length(), static_cast<unsigned int>(UINT16_MAX)));
  const uint16_t authorLen = static_cast<uint16_t>(min(message.author.length(), static_cast<unsigned int>(UINT16_MAX)));
  const uint16_t channelLen = static_cast<uint16_t>(min(message.channel.length(), static_cast<unsigned int>(UINT16_MAX)));
  const uint32_t bodyLen = static_cast<uint32_t>(message.body.length());
  const size_t len = kMessageRecordHeaderSize + createdLen + authorLen + channelLen + bodyLen;
  uint8_t* data = allocMessageBuffer(len);
  if (!data) {
    return false;
  }

  size_t offset = 0;
  std::memcpy(data + offset, kMessageRecordMagic, sizeof(kMessageRecordMagic));
  offset += sizeof(kMessageRecordMagic);
  data[offset++] = kMessageCacheVersion;
  writeU32(data, offset, static_cast<uint32_t>(message.id));
  writeU16(data, offset, createdLen);
  writeU16(data, offset, authorLen);
  writeU16(data, offset, channelLen);
  writeU32(data, offset, bodyLen);
  std::memcpy(data + offset, message.createdAt.c_str(), createdLen);
  offset += createdLen;
  std::memcpy(data + offset, message.author.c_str(), authorLen);
  offset += authorLen;
  std::memcpy(data + offset, message.channel.c_str(), channelLen);
  offset += channelLen;
  std::memcpy(data + offset, message.body.c_str(), bodyLen);

  const bool ok = sd_->writeBinaryAtomic(messagePath(message.id).c_str(), data, len);
  heap_caps_free(data);
  return ok;
}

bool AppStorage::loadMessageRecord(int id, HubMessage& message) const {
  uint8_t* data = nullptr;
  size_t len = 0;
  if (!sd_->readBinaryBuffer(messagePath(id).c_str(), data, len, kMaxMessageRecordBytes, true)) {
    return false;
  }

  size_t offset = 0;
  bool ok = len >= kMessageRecordHeaderSize &&
            std::memcmp(data, kMessageRecordMagic, sizeof(kMessageRecordMagic)) == 0 &&
            data[4] == kMessageCacheVersion;
  offset = 5;
  uint32_t storedId = 0;
  uint16_t createdLen = 0;
  uint16_t authorLen = 0;
  uint16_t channelLen = 0;
  uint32_t bodyLen = 0;
  ok = ok && readU32(data, len, offset, storedId);
  ok = ok && readU16(data, len, offset, createdLen);
  ok = ok && readU16(data, len, offset, authorLen);
  ok = ok && readU16(data, len, offset, channelLen);
  ok = ok && readU32(data, len, offset, bodyLen);
  ok = ok && offset + createdLen + authorLen + channelLen + bodyLen <= len;

  if (ok) {
    message = {};
    message.id = static_cast<int>(storedId);
    message.createdAt = String(reinterpret_cast<const char*>(data + offset), createdLen);
    offset += createdLen;
    message.author = String(reinterpret_cast<const char*>(data + offset), authorLen);
    offset += authorLen;
    message.channel = String(reinterpret_cast<const char*>(data + offset), channelLen);
    offset += channelLen;
    message.body = String(reinterpret_cast<const char*>(data + offset), bodyLen);
    ok = message.id > 0 && message.body.length() > 0;
  }

  sd_->freeBuffer(data);
  return ok;
}

bool AppStorage::saveWeather(const HubWeather& weather) {
  if (!isReady() || !weather.valid) {
    return false;
  }

  JsonDocument doc;
  doc["version"] = kSchemaVersion;
  doc["valid"] = weather.valid;
  doc["location"] = weather.location;
  doc["condition"] = weather.condition;
  doc["icon"] = weather.icon;
  doc["temperature"] = weather.temperature;
  doc["humidity"] = weather.humidity;
  doc["updated_at"] = weather.updatedAt;

  JsonArray hourly = doc["hourly"].to<JsonArray>();
  for (size_t i = 0; i < weather.hourlyCount && i < HubWeather::MaxHourly; ++i) {
    const HubWeatherHourly& source = weather.hourly[i];
    JsonObject item = hourly.add<JsonObject>();
    item["time"] = source.time;
    item["condition"] = source.condition;
    item["icon"] = source.icon;
    item["temperature"] = source.temperature;
    item["humidity"] = source.humidity;
    item["precipitation"] = source.precipitation;
    item["precip_probability"] = source.precipProbability;
    item["wind_direction"] = source.windDirection;
    item["wind_scale"] = source.windScale;
    item["wind_speed"] = source.windSpeed;
  }

  JsonArray daily = doc["daily"].to<JsonArray>();
  for (size_t i = 0; i < weather.dailyCount && i < HubWeather::MaxDaily; ++i) {
    const HubWeatherDaily& source = weather.daily[i];
    JsonObject item = daily.add<JsonObject>();
    item["date"] = source.date;
    item["sunrise"] = source.sunrise;
    item["sunset"] = source.sunset;
    item["condition_day"] = source.conditionDay;
    item["condition_night"] = source.conditionNight;
    item["icon_day"] = source.iconDay;
    item["icon_night"] = source.iconNight;
    item["temperature_min"] = source.temperatureMin;
    item["temperature_max"] = source.temperatureMax;
    item["humidity"] = source.humidity;
    item["precipitation"] = source.precipitation;
    item["precip_probability"] = source.precipProbability;
    item["wind_direction_day"] = source.windDirectionDay;
    item["wind_scale_day"] = source.windScaleDay;
    item["wind_speed_day"] = source.windSpeedDay;
    item["wind_direction_night"] = source.windDirectionNight;
    item["wind_scale_night"] = source.windScaleNight;
    item["wind_speed_night"] = source.windSpeedNight;
  }

  String text;
  serializeJson(doc, text);
  return sd_->writeTextAtomic(WeatherPath, text);
}

bool AppStorage::loadWeather(HubWeather& out) const {
  out = {};
  if (!isReady()) {
    return false;
  }

  String text;
  if (!sd_->readText(WeatherPath, text, kMaxWeatherCacheBytes)) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, text);
  if (error) {
    return false;
  }

  out.location = doc["location"] | "";
  out.condition = doc["condition"] | "";
  out.icon = doc["icon"] | "";
  out.temperature = doc["temperature"] | 0;
  out.humidity = doc["humidity"] | 0;
  out.updatedAt = doc["updated_at"] | "";

  JsonArray hourly = doc["hourly"].as<JsonArray>();
  for (JsonObject item : hourly) {
    if (out.hourlyCount >= HubWeather::MaxHourly) {
      break;
    }
    HubWeatherHourly& target = out.hourly[out.hourlyCount++];
    target.time = item["time"] | "";
    target.condition = item["condition"] | "";
    target.icon = item["icon"] | "";
    target.temperature = item["temperature"] | 0;
    target.humidity = item["humidity"] | 0;
    target.precipitation = item["precipitation"] | 0.0f;
    target.precipProbability = item["precip_probability"] | -1;
    target.windDirection = item["wind_direction"] | "";
    target.windScale = item["wind_scale"] | "";
    target.windSpeed = item["wind_speed"] | 0;
  }

  JsonArray daily = doc["daily"].as<JsonArray>();
  for (JsonObject item : daily) {
    if (out.dailyCount >= HubWeather::MaxDaily) {
      break;
    }
    HubWeatherDaily& target = out.daily[out.dailyCount++];
    target.date = item["date"] | "";
    target.sunrise = item["sunrise"] | "";
    target.sunset = item["sunset"] | "";
    target.conditionDay = item["condition_day"] | "";
    target.conditionNight = item["condition_night"] | "";
    target.iconDay = item["icon_day"] | "";
    target.iconNight = item["icon_night"] | "";
    target.temperatureMin = item["temperature_min"] | 0;
    target.temperatureMax = item["temperature_max"] | 0;
    target.humidity = item["humidity"] | 0;
    target.precipitation = item["precipitation"] | 0.0f;
    target.precipProbability = item["precip_probability"] | -1;
    target.windDirectionDay = item["wind_direction_day"] | "";
    target.windScaleDay = item["wind_scale_day"] | "";
    target.windSpeedDay = item["wind_speed_day"] | 0;
    target.windDirectionNight = item["wind_direction_night"] | "";
    target.windScaleNight = item["wind_scale_night"] | "";
    target.windSpeedNight = item["wind_speed_night"] | 0;
  }

  out.valid = doc["valid"] | (out.condition.length() > 0 || out.hourlyCount > 0 || out.dailyCount > 0);
  return out.valid;
}

bool AppStorage::saveTodos(const HubTodo* todos, size_t count) {
  if (!isReady() || (!todos && count > 0)) {
    return false;
  }

  JsonDocument doc;
  doc["version"] = kSchemaVersion;
  JsonArray items = doc["todos"].to<JsonArray>();
  const size_t limit = count < HubService::MaxTodos ? count : HubService::MaxTodos;
  for (size_t i = 0; i < limit; ++i) {
    JsonObject item = items.add<JsonObject>();
    item["id"] = todos[i].id;
    item["text"] = todos[i].text;
    item["status"] = todos[i].status;
    item["version"] = todos[i].version;
    item["created_at"] = todos[i].createdAt;
    item["updated_at"] = todos[i].updatedAt;
  }

  String text;
  serializeJson(doc, text);
  return sd_->writeTextAtomic(TodosPath, text);
}

bool AppStorage::loadTodos(HubTodo* out, size_t maxCount, size_t& outCount) const {
  outCount = 0;
  if (!isReady() || !out || maxCount == 0) {
    return false;
  }

  String text;
  if (!sd_->readText(TodosPath, text, kMaxTodoCacheBytes)) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, text);
  if (error) {
    return false;
  }

  JsonArray todos = doc["todos"].as<JsonArray>();
  if (todos.isNull()) {
    return false;
  }

  for (JsonObject item : todos) {
    if (outCount >= maxCount) {
      break;
    }
    HubTodo& target = out[outCount];
    target.id = item["id"] | 0;
    target.text = item["text"] | "";
    target.status = item["status"] | 0;
    target.version = item["version"] | 0;
    target.createdAt = item["created_at"] | "";
    target.updatedAt = item["updated_at"] | "";
    target.dirty = false;
    if (target.id > 0 && target.text.length() > 0 && target.version > 0) {
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

  char line[96];
  snprintf(line,
           sizeof(line),
           "%s,%lu,%d,%lu,%.3f,%.2f,%d",
           timestampOrUptime(now, uptimeS).c_str(),
           static_cast<unsigned long>(uptimeS),
           battery.rawAdc,
           static_cast<unsigned long>(battery.rawVoltageMv),
           battery.voltage,
           battery.percentFloat,
           battery.charging ? 1 : 0);
  return sd_->appendLine(path.c_str(), line);
}

bool AppStorage::appendSystemLog(const RtcDateTime& now,
                                 uint32_t uptimeS,
                                 const char* level,
                                 const char* event,
                                 const char* detail) {
  if (!isReady()) {
    return false;
  }

  char line[192];
  snprintf(line,
           sizeof(line),
           "%s,%lu,%s,%s,%s",
           timestampOrUptime(now, uptimeS).c_str(),
           static_cast<unsigned long>(uptimeS),
           level && level[0] ? level : "INFO",
           event && event[0] ? event : "-",
           detail && detail[0] ? detail : "-");
  return sd_->appendLine(systemLogPath(now).c_str(), line);
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

String AppStorage::messagePath(int id) const {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%s/msg_%d.bin", MessagesDir, id);
  return String(buffer);
}

String AppStorage::systemLogPath(const RtcDateTime& now) const {
  char buffer[48];
  if (now.valid) {
    snprintf(buffer, sizeof(buffer), "/tinypanel/logs/system_%04u%02u%02u.log", now.year, now.month, now.day);
  } else {
    snprintf(buffer, sizeof(buffer), "/tinypanel/logs/system_uptime.log");
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
