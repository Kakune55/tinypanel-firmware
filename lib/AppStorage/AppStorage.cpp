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
  return sd_->writeTextAtomic(MessagesPath, text);
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
  if (!sd_->readText(WeatherPath, text, 12000)) {
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
  if (!sd_->readText(TodosPath, text, 8192)) {
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
