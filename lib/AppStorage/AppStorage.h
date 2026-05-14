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

class AppStorage {
 public:
  bool begin(SdCardStorage& sd);
  bool isReady() const;

  bool loadWifiCredentials(StoredWifiCredentials& out) const;
  bool saveMessages(const HubMessage* messages, size_t count);
  bool loadMessages(HubMessage* out, size_t maxCount, size_t& outCount) const;
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
  static constexpr const char* WifiPath = "/tinypanel/config/wifi.json";
  static constexpr const char* MessagesPath = "/tinypanel/cache/messages.json";
  static constexpr const char* BatteryCurvePath = "/tinypanel/calib/battery_curve.csv";

  String batteryLogPath(const RtcDateTime& now) const;
  String systemLogPath(const RtcDateTime& now) const;
  String timestampOrUptime(const RtcDateTime& now, uint32_t uptimeS) const;
  bool parseBatteryCurveLine(const String& line, BatteryCurvePoint& out) const;

  SdCardStorage* sd_ = nullptr;
  bool ready_ = false;
};
