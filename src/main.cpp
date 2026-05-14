#include <Arduino.h>
#include <cstdarg>
#include <cstring>

#include "AppController.h"
#include "AppStorage.h"
#include "BoardConfig.h"
#include "I2cScanner.h"
#include "WifiCredential.h"

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
constexpr uint32_t kHubTelemetryMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kHubMessagePollMs = 60UL * 1000UL;
constexpr uint32_t kHubWeatherPollMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kHubTodoPollMs = 60UL * 1000UL;
constexpr uint32_t kHubSyncIconMinMs = 3000;
constexpr uint8_t kHubMessageLimit = 10;
constexpr uint8_t kHubTodoLimit = 12;
constexpr const char* kHubMessageChannel = "desk";
constexpr size_t kBootLogLines = 12;

BatteryMonitor battery;
AppStorage appStorage;
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

AppControllerConfig makeAppControllerConfig() {
  AppControllerConfig config;
  config.deviceId = kDeviceId;
  config.timezone = kTimezone;
  config.wifiConfigured = APP_HAS_WIFI_SECRETS;
  return config;
}

AppController controller(makeAppControllerConfig(),
                         battery,
                         appStorage,
                         bootButton,
                         keyButton,
                         hub,
                         display,
                         rtc,
                         sdCard,
                         shtc3,
                         timeSync,
                         wifi,
                         ui);

String bootLogLines[kBootLogLines];
size_t bootLogCount = 0;
StoredWifiCredentials sdWifiCredentials;
WifiCredential appSecretsWifiCredential = {AppSecrets::WifiSsid, AppSecrets::WifiPassword};
BatteryCurvePoint sdBatteryCurve[BatteryMonitor::MaxExternalCurvePoints];
HubMessage sdCachedMessages[HubService::MaxMessages];

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

  controller.setBootScreenActive(true);
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
  controller.setSdMounted(sdCard.begin());
  sdCard.printInfo(Serial);
  bootLogf("sd: %s", controller.sdMounted() ? "mounted" : sdCard.lastErrorText());
  bool batteryCurveFromSd = false;
  bool messagesRestoredFromSd = false;
  if (controller.sdMounted()) {
    const bool storageOk = appStorage.begin(sdCard);
    bootLogf("storage: %s", storageOk ? "ready" : "failed");

    size_t curveCount = 0;
    if (appStorage.loadBatteryCurve(sdBatteryCurve, BatteryMonitor::MaxExternalCurvePoints, curveCount) &&
        battery.setBatteryCurve(sdBatteryCurve, curveCount)) {
      batteryCurveFromSd = true;
      bootLogf("battery curve: sd %u", static_cast<unsigned>(curveCount));
    } else {
      bootLog("battery curve: built-in");
    }

    size_t cachedMessageCount = 0;
    if (appStorage.loadMessages(sdCachedMessages, HubService::MaxMessages, cachedMessageCount)) {
      hub.setMessages(sdCachedMessages, cachedMessageCount);
      messagesRestoredFromSd = true;
      bootLogf("messages: cached %u", static_cast<unsigned>(cachedMessageCount));
    }
  }

  const bool sdWifiOk = appStorage.loadWifiCredentials(sdWifiCredentials);
  const bool appSecretsWifiOk = AppSecrets::WifiSsid[0] != '\0' && strncmp(AppSecrets::WifiSsid, "YOUR_", 5) != 0;
  const WifiCredential* wifiCredentials = sdWifiOk ? sdWifiCredentials.credentials : &appSecretsWifiCredential;
  const size_t wifiCredentialCount = sdWifiOk ? sdWifiCredentials.count : (appSecretsWifiOk ? 1 : 0);
  controller.setWifiConfigured(wifiCredentialCount > 0);
  controller.setStorageConfigStatus(sdWifiOk, batteryCurveFromSd, messagesRestoredFromSd);

  if (wifiCredentialCount > 0) {
    bootLog("wifi: connect timeout 12s");
    const bool wifiOk = wifi.begin(wifiCredentials, wifiCredentialCount, 12000);
    bootLogf("wifi: %s", wifiOk ? wifi.ipAddress().c_str() : "failed");
    if (sdWifiOk) {
      Serial.println("WiFi: loaded credentials from SD");
    }
  } else {
    Serial.println("WiFi: create include/AppSecrets.h or /tinypanel/config/wifi.json to enable network");
    bootLog("wifi: not configured");
  }

  bootLog("hub: configure client");
  hub.begin(AppSecrets::HubServerBaseURL, AppSecrets::HubServerApiKey, kDeviceId);
  hub.configureTelemetry(kHubTelemetryMs, kHubSyncIconMinMs);
  hub.configureMessages(kHubMessageChannel, kHubMessagePollMs, kHubMessageLimit);
  hub.configureWeather(kHubWeatherPollMs);
  hub.configureTodos(kHubTodoPollMs, kHubTodoLimit);
  Serial.printf("Hub: telemetry %s\n", hub.isConfigured() ? "configured" : "disabled");
  bootLogf("hub: %s", hub.isConfigured() ? "configured" : "disabled");

  bootLog("sensors: first read");
  controller.readSensors(true);
  bootLog("ntp: sync timeout 12s");
  controller.trySyncTime(true);
  bootLogf("ntp: %s", controller.ntpSynced() ? "synced" : "failed");
  controller.makeBootIdFromCurrentTime();

  bootLog("ui: start desktop");
  controller.setBootScreenActive(false);
  controller.renderUi();
}

void loop() {
  controller.loopOnce();
}
