#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "Shtc3Sensor.h"

struct HubTelemetrySnapshot {
  const char* deviceId = "tinypanel-001";
  String bootId;
  String reportTimestamp;
  uint32_t uptimeS = 0;

  BatteryStatus battery;
  bool usbConnected = false;

  Shtc3Reading environment;

  bool wifiConnected = false;
  String wifiSsid;
  int wifiRssiDbm = 0;
  String wifiIp;

  uint32_t freeHeapBytes = 0;
  uint32_t freePsramBytes = 0;
  bool ntpSync = false;

  bool sdCardPresent = false;
  uint32_t sdCardTotalMb = 0;
  uint32_t sdCardUsedMb = 0;
};

struct HubRequestResult {
  bool attempted = false;
  bool ok = false;
  int statusCode = 0;
};

using HubStateChangedCallback = void (*)();

enum class HubSyncState {
  Idle,
  Syncing,
  Failed,
};

class HubService {
public:
  void begin(const char* baseUrl, const char* apiKey, const char* deviceId);
  void configureTelemetry(uint32_t intervalMs, uint32_t syncIconMinMs);
  bool isConfigured() const;
  bool isSyncing() const;
  bool hasFailed() const;
  bool update(uint32_t nowMs = millis());
  HubRequestResult syncTelemetry(const HubTelemetrySnapshot& snapshot,
                                 bool force,
                                 bool networkReady,
                                 HubStateChangedCallback onStateChanged = nullptr,
                                 uint32_t nowMs = millis());

private:
  HubRequestResult sendTelemetry(const HubTelemetrySnapshot& snapshot);
  HubRequestResult postJson(const char* path, const String& body, const char* label);
  bool telemetryDue(bool force, uint32_t nowMs) const;
  void beginRequest(uint32_t nowMs, HubStateChangedCallback onStateChanged);
  void completeRequest(const HubRequestResult& result, uint32_t nowMs);
  bool timeReached(uint32_t nowMs, uint32_t targetMs) const;
  bool hasUsableCredential(const char* value) const;

  String baseUrl_;
  String apiKey_;
  String deviceId_;
  uint32_t sequence_ = 0;
  uint32_t telemetryIntervalMs_ = 5UL * 60UL * 1000UL;
  uint32_t syncIconMinMs_ = 3000;
  uint32_t lastTelemetryMs_ = 0;
  uint32_t syncMinUntilMs_ = 0;
  HubSyncState syncState_ = HubSyncState::Idle;
  bool requestResultPending_ = false;
  bool lastRequestOk_ = true;
};
