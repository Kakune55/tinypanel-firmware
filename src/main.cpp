#include <Arduino.h>
#include <Wire.h>

#include "BatteryMonitor.h"
#include "Button.h"
#include "BoardConfig.h"
#include "DesktopClockUi.h"
#include "HubService.h"
#include "I2cScanner.h"
#include "RlcdDisplay.h"
#include "RtcClock.h"
#include "SdCardStorage.h"
#include "Shtc3Sensor.h"
#include "TimeSync.h"
#include "WifiManager.h"

#if __has_include("AppSecrets.h")
#include "AppSecrets.h"
#define APP_HAS_WIFI_SECRETS 1
#else
#define APP_HAS_WIFI_SECRETS 0
namespace AppSecrets {
constexpr const char* WifiSsid = "";
constexpr const char* WifiPassword = "";
constexpr const char* HubServerBaseURL = "";
constexpr const char* HubServerApiKey = "";
}  // namespace AppSecrets
#endif

namespace {

constexpr const char* kDeviceId = "tinypanel-001";
constexpr const char* kTimezone = "CST-8";
constexpr uint32_t kBatteryPollMs = 60000;
constexpr uint32_t kEnvironmentPollMs = 30000;
constexpr uint32_t kRtcPollMs = 1000;
constexpr uint32_t kWifiRetryMs = 30000;
constexpr uint32_t kNtpRetryMs = 10UL * 60UL * 1000UL;

BatteryMonitor battery;
Button bootButton(BoardConfig::ButtonBoot);
Button keyButton(BoardConfig::ButtonKey);
HubService hub;
I2cScanner i2cScanner;
RlcdDisplay display;
RtcClock rtc;
SdCardStorage sdCard;
Shtc3Sensor shtc3;
TimeSync timeSync;
WifiManager wifi;
DesktopClockUi ui(display);

struct AppState {
  BatteryStatus battery;
  Shtc3Reading environment;
  RtcDateTime now;
  DesktopClockPage page = DesktopClockPage::Clock;
  bool ntpSynced = false;
  bool ntpSyncing = false;
  bool ntpSyncFailed = false;
  bool sdMounted = false;
  bool uiDirty = true;
  uint32_t lastBatteryMs = 0;
  uint32_t lastEnvironmentMs = 0;
  uint32_t lastRtcMs = 0;
  uint32_t lastWifiRetryMs = 0;
  uint32_t lastNtpAttemptMs = 0;
  String bootId;
};

AppState app;

String formatRtcTimestamp(const RtcDateTime& dt) {
  if (!dt.valid) {
    return "";
  }

  char buffer[32];
  snprintf(buffer,
           sizeof(buffer),
           "%04u-%02u-%02uT%02u:%02u:%02u+08:00",
           dt.year,
           dt.month,
           dt.day,
           dt.hour,
           dt.minute,
           dt.second);
  return String(buffer);
}

String makeBootId(const RtcDateTime& dt) {
  if (dt.valid) {
    char buffer[28];
    snprintf(buffer,
             sizeof(buffer),
             "boot_%04u%02u%02u_%02u%02u%02u",
             dt.year,
             dt.month,
             dt.day,
             dt.hour,
             dt.minute,
             dt.second);
    return String(buffer);
  }

  return String("boot_uptime_") + String(millis() / 1000UL);
}

DesktopClockUiModel buildUiModel() {
  DesktopClockUiModel model;
  model.page = app.page;
  model.battery = app.battery;
  model.environment = app.environment;
  model.now = app.now;
  model.ntpSynced = app.ntpSynced;
  model.ntpSyncing = app.ntpSyncing;
  model.ntpSyncFailed = app.ntpSyncFailed;
  model.hubSyncing = hub.isSyncing();
  model.hubSyncFailed = hub.hasFailed();
  model.sdMounted = app.sdMounted;
  model.sdStatus = sdCard.lastErrorText();
  model.wifiConnected = wifi.isConnected();
  model.wifiRssi = wifi.rssi();
  model.wifiIp = wifi.isConnected() ? wifi.ipAddress() : "";
  model.wifiSsid = wifi.ssid();
  model.uptimeMs = millis();
  model.freeHeap = ESP.getFreeHeap();
  model.freePsram = ESP.getFreePsram();
  return model;
}

void renderUi() {
  ui.render(buildUiModel());
  app.uiDirty = false;
}

void renderHubState() {
  app.uiDirty = true;
  if (display.isReady()) {
    renderUi();
  }
}

void readSensors(bool force = false) {
  const uint32_t now = millis();

  if (force || now - app.lastBatteryMs >= kBatteryPollMs) {
    app.battery = battery.readStatus();
    app.lastBatteryMs = now;
    app.uiDirty = true;
  }

  if (force || now - app.lastEnvironmentMs >= kEnvironmentPollMs) {
    shtc3.read(app.environment);
    app.lastEnvironmentMs = now;
    app.uiDirty = true;
  }

  if (force || now - app.lastRtcMs >= kRtcPollMs) {
    RtcDateTime dt;
    rtc.read(dt);
    if (dt.second != app.now.second || dt.valid != app.now.valid || force) {
      app.uiDirty = true;
    }
    app.now = dt;
    app.lastRtcMs = now;
  }
}

bool trySyncTime(bool force = false) {
  const uint32_t now = millis();
  if (!APP_HAS_WIFI_SECRETS || !wifi.isConnected()) {
    if (force) {
      app.ntpSyncFailed = true;
      app.uiDirty = true;
    }
    return false;
  }
  if (!force && app.ntpSynced && now - app.lastNtpAttemptMs < kNtpRetryMs) {
    return true;
  }
  if (!force && now - app.lastNtpAttemptMs < kNtpRetryMs) {
    return false;
  }

  app.lastNtpAttemptMs = now;
  if (!timeSync.begin(kTimezone)) {
    app.ntpSyncFailed = true;
    app.uiDirty = true;
    return false;
  }

  app.ntpSyncing = true;
  app.ntpSyncFailed = false;
  app.uiDirty = true;
  if (display.isReady()) {
    renderUi();
  }

  app.ntpSynced = timeSync.syncToRtc(rtc, 12000);
  app.ntpSyncing = false;
  app.ntpSyncFailed = !app.ntpSynced;
  rtc.read(app.now);
  app.uiDirty = true;
  return app.ntpSynced;
}

void handleWifi() {
  const uint32_t now = millis();
  if (!APP_HAS_WIFI_SECRETS || wifi.isConnected() || now - app.lastWifiRetryMs < kWifiRetryMs) {
    return;
  }

  wifi.connect(5000);
  app.lastWifiRetryMs = now;
  app.uiDirty = true;
}

HubTelemetrySnapshot buildHubTelemetrySnapshot() {
  HubTelemetrySnapshot snapshot;
  snapshot.deviceId = kDeviceId;
  snapshot.bootId = app.bootId;
  snapshot.reportTimestamp = formatRtcTimestamp(app.now);
  snapshot.uptimeS = millis() / 1000UL;

  snapshot.battery = app.battery;
  snapshot.usbConnected = false;

  snapshot.environment = app.environment;

  snapshot.wifiConnected = wifi.isConnected();
  snapshot.wifiSsid = wifi.ssid();
  snapshot.wifiRssiDbm = wifi.rssi();
  snapshot.wifiIp = wifi.ipAddress();

  snapshot.freeHeapBytes = ESP.getFreeHeap();
  snapshot.freePsramBytes = ESP.getFreePsram();
  snapshot.ntpSync = app.ntpSynced;

  snapshot.sdCardPresent = sdCard.isMounted();
  snapshot.sdCardTotalMb = sdCard.cardSizeBytes() / (1024UL * 1024UL);
  snapshot.sdCardUsedMb = sdCard.usedBytes() / (1024UL * 1024UL);
  return snapshot;
}

void syncHubTelemetry(bool force = false) {
  const HubRequestResult result = hub.syncTelemetry(buildHubTelemetrySnapshot(), force, wifi.isConnected(), renderHubState);
  if (result.attempted) {
    app.uiDirty = true;
  }
}

void handleButtons() {
  keyButton.update();
  bootButton.update();

  if (keyButton.consumeReleased()) {
    bool longPress = keyButton.lastPressDurationMs() >= 1000;
    Serial.println(longPress ? "KEY: force network sync" : "KEY: manual refresh");
    if (!sdCard.isMounted()) {
      app.sdMounted = sdCard.begin();
      sdCard.printInfo(Serial);
    }
    readSensors(true);
    if (longPress && !wifi.isConnected() && APP_HAS_WIFI_SECRETS) {
      wifi.connect(8000);
    }
    if (longPress) {
      trySyncTime(true);
      syncHubTelemetry(true);
    }
    app.uiDirty = true;
  }

  if (bootButton.consumeReleased()) {
    app.page = DesktopClockUi::nextPage(app.page);
    app.uiDirty = true;
    Serial.println("BOOT: page switched");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32-S3 RLCD 4.2 Demo UI");
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());

  keyButton.begin();
  bootButton.begin();

  i2cScanner.begin(BoardConfig::I2cSda, BoardConfig::I2cScl);
  i2cScanner.printScan();

  battery.begin();
  Serial.printf("SHTC3 begin: %s\n", shtc3.begin() ? "ok" : "failed");
  Serial.printf("RTC begin: %s\n", rtc.begin() ? "ok" : "failed");
  app.sdMounted = sdCard.begin();
  sdCard.printInfo(Serial);

  if (APP_HAS_WIFI_SECRETS) {
    wifi.begin(AppSecrets::WifiSsid, AppSecrets::WifiPassword, 12000);
  } else {
    Serial.println("WiFi: create include/AppSecrets.h from AppSecrets.example.h to enable network");
  }
  hub.begin(AppSecrets::HubServerBaseURL, AppSecrets::HubServerApiKey, kDeviceId);
  Serial.printf("Hub: telemetry %s\n", hub.isConfigured() ? "configured" : "disabled");

  if (!display.begin()) {
    Serial.println("RLCD init failed");
    return;
  }

  readSensors(true);
  trySyncTime(true);
  app.bootId = makeBootId(app.now);
  renderUi();
  syncHubTelemetry(true);
}

void loop() {
  handleButtons();
  handleWifi();
  trySyncTime(false);
  readSensors(false);
  syncHubTelemetry(false);
  if (hub.update()) {
    app.uiDirty = true;
  }

  if (app.uiDirty) {
    renderUi();
  }

  delay(10);
}
