#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "AppStorage.h"
#include "Button.h"
#include "DesktopClockUi.h"
#include "HubService.h"
#include "RlcdDisplay.h"
#include "RtcClock.h"
#include "SdCardStorage.h"
#include "Shtc3Sensor.h"
#include "TimeSync.h"
#include "WifiManager.h"

struct AppControllerConfig {
  const char* deviceId = "tinypanel-001";
  const char* timezone = "CST-8";
  bool wifiConfigured = false;
  uint32_t rtcPollMs = 1000;
  uint32_t hubSyncWindowMs = 60000;
  uint8_t telemetryEveryHubSyncWindows = 5;
  uint32_t wifiRetryMs = 30000;
  uint32_t ntpRetryMs = 10UL * 60UL * 1000UL;
  uint32_t ntpUnsyncedRetryMs = 30000;
  uint32_t keyDoubleClickMs = 350;
  uint32_t keyLongPressMs = 1000;
  uint32_t newMessageBlinkMs = 500;
  uint32_t batteryLogIntervalMs = 15UL * 60UL * 1000UL;
  uint32_t sdStatsRefreshMs = 30000;
  uint32_t loopDelayMs = 10;
  bool enableLightSleep = false;
  bool enableDynamicCpuFrequency = true;
  uint8_t activeCpuMhz = 240;
  uint8_t idleCpuMhz = 80;
  uint32_t cpuIdleAfterMs = 2000;
};

class AppController {
 public:
  AppController(const AppControllerConfig& config,
                BatteryMonitor& battery,
                AppStorage& storage,
                Button& bootButton,
                Button& keyButton,
                HubService& hub,
                RlcdDisplay& display,
                RtcClock& rtc,
                SdCardStorage& sdCard,
                Shtc3Sensor& shtc3,
                TimeSync& timeSync,
                WifiManager& wifi,
                DesktopClockUi& ui);

  void setBootScreenActive(bool active);
  void applyConfig(const AppControllerConfig& config);
  void setSdMounted(bool mounted);
  void setWifiConfigured(bool configured);
  void setStorageConfigStatus(bool wifiFromSd, bool batteryCurveFromSd, bool messagesRestoredFromSd);
  bool sdMounted() const;
  bool ntpSynced() const;

  void readSensors(bool force = false);
  bool trySyncTime(bool force = false);
  void makeBootIdFromCurrentTime();
  void renderUi();
  void syncHubTelemetry(bool force = false);
  void pollHubMessages(bool force = false);
  void pollWeather(bool force = false);
  void pollTodos(bool force = false);
  void runInitialHubSyncNow();
  void loopOnce();

 private:
  struct State {
    enum class InitialHubSyncStep : uint8_t {
      Telemetry,
      Weather,
      Messages,
      Todos,
      Done,
    };

    enum class ScheduledTaskStep : uint8_t {
      Idle,
      WifiSignal,
      Ntp,
      Sensors,
      Messages,
      TodoSync,
      Todos,
      Weather,
      Telemetry,
    };

    struct BatteryHistoryPoint {
      uint32_t uptimeS = 0;
      float percent = 0.0f;
    };

    BatteryStatus battery;
    Shtc3Reading environment;
    RtcDateTime now;
    DesktopClockPage page = DesktopClockPage::Clock;
    bool ntpSynced = false;
    bool ntpSyncing = false;
    bool ntpSyncFailed = false;
    bool sdMounted = false;
    bool wifiConfigFromSd = false;
    bool batteryCurveFromSd = false;
    bool messagesRestoredFromSd = false;
    bool uiDirty = true;
    uint8_t selectedSystemMenuItem = 0;
    uint8_t selectedSystemAction = 0;
    bool systemActionFocused = false;
    uint32_t lastRtcMs = 0;
    uint32_t lastBatteryLogMs = 0;
    uint32_t lastSdStatsMs = 0;
    uint32_t sdCardTotalMb = 0;
    uint32_t sdCardUsedMb = 0;
    uint32_t lastWifiRetryMs = 0;
    uint32_t lastNtpAttemptMs = 0;
    uint32_t lastHubSyncWindowMs = 0;
    uint32_t pendingKeyClickMs = 0;
    uint32_t lastAlertBlinkMs = 0;
    bool pendingKeyClick = false;
    size_t selectedMessage = 0;
    uint16_t messageBodyScrollLine = 0;
    bool messageBodyFocused = false;
    bool newMessageAlert = false;
    bool pendingNewMessageAlert = false;
    bool messageDeleteTriggered = false;
    uint8_t messageDeleteProgress = 0;
    String bootId;
    size_t selectedTodo = 0;
    uint8_t hubSyncWindowCount = 0;
    InitialHubSyncStep initialHubSyncStep = InitialHubSyncStep::Telemetry;
    ScheduledTaskStep scheduledTaskStep = ScheduledTaskStep::Idle;
    bool scheduledTaskForce = false;
    bool scheduledTaskIncludeTelemetry = false;
    bool scheduledTaskTodoSyncOk = true;
    static constexpr size_t BatteryHistorySize = 180;
    BatteryHistoryPoint batteryHistory[BatteryHistorySize];
    size_t batteryHistoryCount = 0;
    size_t batteryHistoryNext = 0;
    int batteryEtaMinutes = -1;
    bool hasBatteryEtaEstimate = false;
    bool hasBatteryEtaFilter = false;
    bool batteryEtaWasCharging = false;
    float batteryEtaFilteredPercent = 0.0f;
    uint32_t lastBatteryEtaSampleS = 0;
    uint32_t lastActivityMs = 0;
    uint8_t currentCpuMhz = 0;
  };

  static void handleHubStateChanged();

  String formatRtcTimestamp(const RtcDateTime& dt) const;
  String makeBootId(const RtcDateTime& dt) const;
  DesktopClockUiModel buildUiModel() const;
  void renderHubState();
  void handleWifi();
  void readRtc(bool force = false);
  void runScheduledTasks(bool force = false, bool includeTelemetry = false);
  bool runNextInitialHubSyncStep();
  bool runNextScheduledTask();
  void queueScheduledTasks(bool force, bool includeTelemetry);
  void publishPendingNewMessageAlert();
  bool verifySdMounted();
  void refreshSdStats(bool force = false);
  void handleForcedRefresh();
  void updateSelectedTodoAfterChange();
  void updateBatteryRuntimeEstimate();
  HubTelemetrySnapshot buildHubTelemetrySnapshot() const;
  uint16_t messageBodyLineCount(const String& text) const;
  void handleMessageKeyClick();
  void handleMessageDelete();
  void handleMessageDeleteHold();
  void handleTodoKeyClick();
  void handleTodoStatusToggle();
  void handleTodoDelete();
  void handleSystemKeyClick();
  void handleSystemAction();
  void handleSystemClearMessages();
  void handleSingleKeyClick();
  void handleKeyDoubleClick();
  void handlePendingKeyClick();
  void handleButtons();
  void sleepUntilNextDeadline();
  uint32_t msUntil(uint32_t targetMs, uint32_t nowMs) const;
  bool canLightSleep() const;
  void noteActivity();
  void updateCpuFrequency();
  void applyCpuFrequency(uint8_t mhz);
  bool shouldUseActiveCpu() const;
  void markUiDirty();

  AppControllerConfig config_;
  BatteryMonitor& battery_;
  AppStorage& storage_;
  Button& bootButton_;
  Button& keyButton_;
  HubService& hub_;
  RlcdDisplay& display_;
  RtcClock& rtc_;
  SdCardStorage& sdCard_;
  Shtc3Sensor& shtc3_;
  TimeSync& timeSync_;
  WifiManager& wifi_;
  DesktopClockUi& ui_;
  bool bootScreenActive_ = false;
  State state_;
};
