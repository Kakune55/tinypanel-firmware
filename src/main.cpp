#include <Arduino.h>
#include <Wire.h>
#include <cstdarg>

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
constexpr uint32_t kNtpUnsyncedRetryMs = 30000;
constexpr uint32_t kHubTelemetryMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kHubMessagePollMs = 60UL * 1000UL;
constexpr uint32_t kHubSyncIconMinMs = 3000;
constexpr uint32_t kKeyDoubleClickMs = 350;
constexpr uint32_t kKeyLongPressMs = 1000;
constexpr uint32_t kNewMessageBlinkMs = 500;
constexpr uint8_t kHubMessageLimit = 10;
constexpr const char* kHubMessageChannel = "desk";
constexpr size_t kBootLogLines = 12;

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
  uint32_t pendingKeyClickMs = 0;
  bool pendingKeyClick = false;
  size_t selectedMessage = 0;
  uint16_t messageBodyScrollLine = 0;
  bool messageBodyFocused = false;
  bool newMessageAlert = false;
  String bootId;
};

AppState app;
bool bootScreenActive = false;
String bootLogLines[kBootLogLines];
size_t bootLogCount = 0;

void drawBootLog() {
  if (!display.isReady()) {
    return;
  }

  display.clear(true);
  display.drawText(10, 10, "TinyPanel boot", true, 2);
  display.drawFastHLine(10, 32, 380, true);

  const size_t start = bootLogCount > kBootLogLines ? bootLogCount - kBootLogLines : 0;
  for (size_t i = start; i < bootLogCount; ++i) {
    const size_t row = i - start;
    display.drawText(12, 44 + static_cast<int>(row) * 18, bootLogLines[i % kBootLogLines].c_str(), true, 2);
  }

  display.flushFull();
}

void bootLog(const char* text) {
  Serial.println(text);
  bootLogLines[bootLogCount % kBootLogLines] = text;
  ++bootLogCount;
  drawBootLog();
}

void bootLogf(const char* format, ...) {
  char buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  bootLog(buffer);
}

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
  model.newMessageAlert = app.newMessageAlert;
  model.newMessageAlertInvert = app.newMessageAlert && ((millis() / kNewMessageBlinkMs) % 2 == 1);
  model.messages = hub.messageAt(0);
  model.messageCount = hub.messageCount();
  model.selectedMessage = app.selectedMessage;
  model.messageBodyFocused = app.messageBodyFocused;
  model.messageBodyScrollLine = app.messageBodyScrollLine;
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
  const uint32_t retryMs = app.ntpSynced ? kNtpRetryMs : kNtpUnsyncedRetryMs;
  if (!force && now - app.lastNtpAttemptMs < retryMs) {
    return app.ntpSynced;
  }

  if (!force && !wifi.isConnected()) {
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
  if (display.isReady() && !bootScreenActive) {
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

void pollHubMessages(bool force = false) {
  const size_t before = hub.messageCount();
  const HubRequestResult result = hub.pollMessages(force, wifi.isConnected(), renderHubState);
  if (!result.attempted) {
    return;
  }

  if (hub.messageCount() != before) {
    app.selectedMessage = 0;
    app.messageBodyScrollLine = 0;
    if (app.page != DesktopClockPage::Message) {
      app.newMessageAlert = true;
    }
  }
  app.uiDirty = true;
}

uint16_t messageBodyLineCount(const String& text) {
  constexpr uint16_t kCharsPerLine = 19;
  uint16_t lines = 1;
  uint16_t col = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      ++lines;
      col = 0;
      continue;
    }
    ++col;
    if (col >= kCharsPerLine) {
      ++lines;
      col = 0;
    }
  }
  return lines;
}

void handleMessageKeyClick() {
  const size_t count = hub.messageCount();
  if (count == 0) {
    return;
  }

  if (!app.messageBodyFocused) {
    app.selectedMessage = (app.selectedMessage + 1) % count;
    app.messageBodyScrollLine = 0;
    app.uiDirty = true;
    return;
  }

  const HubMessage* message = hub.messageAt(app.selectedMessage);
  const uint16_t lineCount = message ? messageBodyLineCount(message->body) : 1;
  constexpr uint16_t kPageLines = 9;
  if (lineCount <= kPageLines || app.messageBodyScrollLine + kPageLines >= lineCount) {
    app.messageBodyScrollLine = 0;
  } else {
    app.messageBodyScrollLine += kPageLines;
  }
  app.uiDirty = true;
}

void handleSingleKeyClick() {
  if (app.newMessageAlert) {
    app.newMessageAlert = false;
    app.page = DesktopClockPage::Message;
    app.messageBodyFocused = false;
    app.selectedMessage = 0;
    app.messageBodyScrollLine = 0;
    app.uiDirty = true;
    Serial.println("KEY: open new message");
    return;
  }

  if (app.page == DesktopClockPage::Message) {
    handleMessageKeyClick();
    return;
  }

  Serial.println("KEY: manual refresh");
  if (!sdCard.isMounted()) {
    app.sdMounted = sdCard.begin();
    sdCard.printInfo(Serial);
  }
  readSensors(true);
  pollHubMessages(true);
  app.uiDirty = true;
}

void handleKeyDoubleClick() {
  if (app.page != DesktopClockPage::Message) {
    handleSingleKeyClick();
    return;
  }

  app.messageBodyFocused = !app.messageBodyFocused;
  app.uiDirty = true;
  Serial.println(app.messageBodyFocused ? "KEY: message body focus" : "KEY: message list focus");
}

void handlePendingKeyClick() {
  if (!app.pendingKeyClick) {
    return;
  }
  if (millis() - app.pendingKeyClickMs < kKeyDoubleClickMs) {
    return;
  }

  app.pendingKeyClick = false;
  handleSingleKeyClick();
}

void handleButtons() {
  keyButton.update();
  bootButton.update();

  if (keyButton.consumeReleased()) {
    const uint32_t now = millis();
    bool longPress = keyButton.lastPressDurationMs() >= kKeyLongPressMs;
    if (longPress) {
      app.pendingKeyClick = false;
      Serial.println("KEY: force network sync");
      if (!sdCard.isMounted()) {
        app.sdMounted = sdCard.begin();
        sdCard.printInfo(Serial);
      }
      readSensors(true);
    }
    if (longPress && !wifi.isConnected() && APP_HAS_WIFI_SECRETS) {
      wifi.connect(8000);
    }
    if (longPress) {
      trySyncTime(true);
      syncHubTelemetry(true);
      pollHubMessages(true);
      app.uiDirty = true;
    } else if (app.newMessageAlert) {
      app.pendingKeyClick = false;
      handleSingleKeyClick();
    } else if (app.pendingKeyClick && now - app.pendingKeyClickMs < kKeyDoubleClickMs) {
      app.pendingKeyClick = false;
      handleKeyDoubleClick();
    } else {
      app.pendingKeyClick = true;
      app.pendingKeyClickMs = now;
    }
  }

  if (bootButton.consumeReleased()) {
    app.page = DesktopClockUi::nextPage(app.page);
    if (app.page == DesktopClockPage::Message) {
      app.newMessageAlert = false;
    }
    app.pendingKeyClick = false;
    app.uiDirty = true;
    Serial.println("BOOT: page switched");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("ESP32-S3 RLCD 4.2 Demo UI");
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());

  keyButton.begin();
  bootButton.begin();

  if (!display.begin()) {
    Serial.println("RLCD init failed");
    return;
  }

  bootScreenActive = true;
  bootLog("kernel: TinyPanel firmware");
  bootLogf("psram: %s", psramFound() ? "yes" : "no");
  bootLogf("flash: %lu KB", static_cast<unsigned long>(ESP.getFlashChipSize() / 1024UL));

  bootLog("i2c: scanning bus");
  i2cScanner.begin(BoardConfig::I2cSda, BoardConfig::I2cScl);
  i2cScanner.printScan();

  bootLog("power: init battery adc");
  battery.begin();

  bootLog("shtc3: probing sensor");
  const bool shtc3Ok = shtc3.begin();
  Serial.printf("SHTC3 begin: %s\n", shtc3Ok ? "ok" : "failed");
  bootLogf("shtc3: %s", shtc3Ok ? "ok" : "failed");

  bootLog("rtc: probing clock");
  const bool rtcOk = rtc.begin();
  Serial.printf("RTC begin: %s\n", rtcOk ? "ok" : "failed");
  bootLogf("rtc: %s", rtcOk ? "ok" : "failed");

  bootLog("sd: mount card");
  app.sdMounted = sdCard.begin();
  sdCard.printInfo(Serial);
  bootLogf("sd: %s", app.sdMounted ? "mounted" : sdCard.lastErrorText());

  if (APP_HAS_WIFI_SECRETS) {
    bootLog("wifi: connect timeout 12s");
    const bool wifiOk = wifi.begin(AppSecrets::WifiSsid, AppSecrets::WifiPassword, 12000);
    bootLogf("wifi: %s", wifiOk ? wifi.ipAddress().c_str() : "failed");
  } else {
    Serial.println("WiFi: create include/AppSecrets.h from AppSecrets.example.h to enable network");
    bootLog("wifi: not configured");
  }

  bootLog("hub: configure client");
  hub.begin(AppSecrets::HubServerBaseURL, AppSecrets::HubServerApiKey, kDeviceId);
  hub.configureTelemetry(kHubTelemetryMs, kHubSyncIconMinMs);
  hub.configureMessages(kHubMessageChannel, kHubMessagePollMs, kHubMessageLimit);
  Serial.printf("Hub: telemetry %s\n", hub.isConfigured() ? "configured" : "disabled");
  bootLogf("hub: %s", hub.isConfigured() ? "configured" : "disabled");

  bootLog("sensors: first read");
  readSensors(true);
  bootLog("ntp: sync timeout 12s");
  trySyncTime(true);
  bootLogf("ntp: %s", app.ntpSynced ? "synced" : "failed");
  app.bootId = makeBootId(app.now);

  bootLog("ui: start desktop");
  bootScreenActive = false;
  renderUi();
  syncHubTelemetry(true);
  pollHubMessages(true);
}

void loop() {
  handleButtons();
  handlePendingKeyClick();
  handleWifi();
  trySyncTime(false);
  readSensors(false);
  syncHubTelemetry(false);
  pollHubMessages(false);
  if (hub.update()) {
    app.uiDirty = true;
  }
  if (app.newMessageAlert) {
    static uint32_t lastAlertBlinkMs = 0;
    const uint32_t now = millis();
    if (now - lastAlertBlinkMs >= kNewMessageBlinkMs) {
      lastAlertBlinkMs = now;
      app.uiDirty = true;
    }
  }

  if (app.uiDirty) {
    renderUi();
  }

  delay(10);
}
