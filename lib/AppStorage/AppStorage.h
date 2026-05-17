#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "HubService.h"
#include "RtcClock.h"
#include "SdCardStorage.h"
#include "WifiCredential.h"

struct StoredWifiCredentials {
  static constexpr size_t MaxNetworks = 5;
  static constexpr size_t MaxSsidLength = 32;
  static constexpr size_t MaxPasswordLength = 64;

  WifiCredential credentials[MaxNetworks];
  char ssids[MaxNetworks][MaxSsidLength + 1];
  char passwords[MaxNetworks][MaxPasswordLength + 1];
  size_t count = 0;
};

struct StoredDeviceConfig {
  static constexpr size_t MaxDeviceIdLength = 32;
  static constexpr size_t MaxTimezoneLength = 32;
  static constexpr size_t MaxMessageChannelLength = 32;

  char deviceId[MaxDeviceIdLength + 1] = "tinypanel-001";
  char timezone[MaxTimezoneLength + 1] = "CST-8";
  char messageChannel[MaxMessageChannelLength + 1] = "desk";
  uint32_t hubTelemetryMs = 5UL * 60UL * 1000UL;
  uint32_t hubMessagePollMs = 60UL * 1000UL;
  uint32_t hubWeatherPollMs = 30UL * 60UL * 1000UL;
  uint32_t hubTodoPollMs = 60UL * 1000UL;
  uint8_t hubMessageLimit = 10;
  uint8_t hubTodoLimit = 12;
  uint32_t batteryLogIntervalMs = 15UL * 60UL * 1000UL;
  bool loaded = false;
};

class AppStorage {
 public:
  bool begin(SdCardStorage& sd);
  bool isReady() const;

  bool loadWifiCredentials(StoredWifiCredentials& out) const;
  bool loadDeviceConfig(StoredDeviceConfig& config) const;
  bool saveMessages(const HubMessage* messages, size_t count);
  bool loadMessages(HubMessage* out, size_t maxCount, size_t& outCount) const;
  bool saveWeather(const HubWeather& weather);
  bool loadWeather(HubWeather& out) const;
  bool saveTodos(const HubTodo* todos, size_t count);
  bool loadTodos(HubTodo* out, size_t maxCount, size_t& outCount) const;
  bool loadBatteryCurve(BatteryCurvePoint* out, size_t maxCount, size_t& outCount) const;
  bool appendBatterySample(const BatteryStatus& battery, const RtcDateTime& now, uint32_t uptimeS);
  bool appendSystemLog(const RtcDateTime& now, uint32_t uptimeS, const char* level, const char* event, const char* detail);

 private:
  static constexpr const char* RootDir = "/tinypanel";
  static constexpr const char* ConfigDir = "/tinypanel/config";
  static constexpr const char* CacheDir = "/tinypanel/cache";
  static constexpr const char* LogsDir = "/tinypanel/logs";
  static constexpr const char* CalibDir = "/tinypanel/calib";
  static constexpr const char* StateDir = "/tinypanel/state";
  static constexpr const char* MessagesDir = "/tinypanel/cache/messages";
  static constexpr const char* WifiPath = "/tinypanel/config/wifi.json";
  static constexpr const char* DevicePath = "/tinypanel/config/device.json";
  static constexpr const char* MessagesIndexPath = "/tinypanel/cache/messages/index.bin";
  static constexpr const char* WeatherPath = "/tinypanel/cache/weather.json";
  static constexpr const char* TodosPath = "/tinypanel/cache/todos.json";
  static constexpr const char* BatteryCurvePath = "/tinypanel/calib/battery_curve.csv";

  String messagePath(int id) const;
  bool saveMessageRecord(const HubMessage& message) const;
  bool loadMessageRecord(int id, HubMessage& message) const;
  String batteryLogPath(const RtcDateTime& now) const;
  String systemLogPath(const RtcDateTime& now) const;
  String timestampOrUptime(const RtcDateTime& now, uint32_t uptimeS) const;
  bool parseBatteryCurveLine(const String& line, BatteryCurvePoint& out) const;

  SdCardStorage* sd_ = nullptr;
  bool ready_ = false;
};
