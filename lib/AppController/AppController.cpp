#include "AppController.h"

#include "BoardConfig.h"
#include "Utf8Text.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {

AppController* activeController = nullptr;
constexpr uint8_t kSystemMenuStorage = 1;
constexpr uint8_t kSystemMenuBattery = 2;
constexpr uint8_t kSystemMenuAction = 3;
constexpr uint8_t kSystemMenuItemCount = 4;
constexpr uint8_t kSystemActionWifiToggle = 0;
constexpr uint8_t kSystemActionSyncNow = 1;
constexpr uint8_t kSystemActionClearMessages = 2;
constexpr uint8_t kSystemActionResetBattery = 3;
constexpr uint8_t kSystemActionRePair = 4;
constexpr uint8_t kSystemActionCanvas = 5;
constexpr uint8_t kSystemActionBack = 6;
constexpr uint8_t kSystemActionCount = 7;
constexpr uint32_t kMessageDeleteProgressShowMs = 400;
constexpr uint32_t kSerialCanvasIdleFlushMs = 60;
constexpr float kBatteryVoltageDirtyDelta = 0.03f;
constexpr float kBatteryPercentDirtyDelta = 1.0f;
constexpr float kTemperatureDirtyDelta = 0.1f;
constexpr float kHumidityDirtyDelta = 0.5f;
constexpr float kBatteryFullHoldPercent = 99.5f;

bool batteryDisplayChanged(const BatteryStatus& before, const BatteryStatus& after) {
  return before.percent != after.percent ||
         before.charging != after.charging ||
         before.low != after.low ||
         before.critical != after.critical ||
         std::fabs(before.voltage - after.voltage) >= kBatteryVoltageDirtyDelta ||
         std::fabs(before.percentFloat - after.percentFloat) >= kBatteryPercentDirtyDelta;
}

bool environmentDisplayChanged(const Shtc3Reading& before, const Shtc3Reading& after) {
  if (before.valid != after.valid) {
    return true;
  }
  if (!after.valid) {
    return false;
  }
  return std::fabs(before.temperatureC - after.temperatureC) >= kTemperatureDirtyDelta ||
         std::fabs(before.humidityRh - after.humidityRh) >= kHumidityDirtyDelta;
}

bool batteryInFullHold(const BatteryStatus& battery) {
  return battery.charging || battery.percentFloat >= kBatteryFullHoldPercent;
}

bool parseCanvasColor(const char* token, bool& black) {
  if (token == nullptr) {
    return false;
  }
  if (strcmp(token, "1") == 0 || strcasecmp(token, "B") == 0 || strcasecmp(token, "BLACK") == 0) {
    black = true;
    return true;
  }
  if (strcmp(token, "0") == 0 || strcasecmp(token, "W") == 0 || strcasecmp(token, "WHITE") == 0) {
    black = false;
    return true;
  }
  return false;
}

bool parseCanvasInt(const char* token, int& value) {
  if (token == nullptr || *token == '\0') {
    return false;
  }
  char* end = nullptr;
  const long parsed = strtol(token, &end, 10);
  if (end == token || *end != '\0') {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool leapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint16_t daysBeforeMonth(int year, int month) {
  static constexpr uint16_t kDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  if (month < 1 || month > 12) {
    return 0;
  }
  return kDays[month - 1] + ((month > 2 && leapYear(year)) ? 1 : 0);
}

uint32_t absoluteMinute(const RtcDateTime& dt, uint32_t fallbackUptimeMs) {
  if (!dt.valid || dt.year < 2000) {
    return fallbackUptimeMs / 60000UL;
  }

  uint32_t days = 0;
  for (int year = 2000; year < dt.year; ++year) {
    days += leapYear(year) ? 366UL : 365UL;
  }
  days += daysBeforeMonth(dt.year, dt.month);
  days += static_cast<uint32_t>(dt.day > 0 ? dt.day - 1 : 0);
  return days * 1440UL + static_cast<uint32_t>(dt.hour) * 60UL + dt.minute;
}

}  // namespace

AppController::AppController(const AppControllerConfig& config,
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
                             DesktopClockUi& ui)
    : config_(config),
      battery_(battery),
      storage_(storage),
      bootButton_(bootButton),
      keyButton_(keyButton),
      hub_(hub),
      display_(display),
      rtc_(rtc),
      sdCard_(sdCard),
      shtc3_(shtc3),
      timeSync_(timeSync),
      wifi_(wifi),
      ui_(ui) {
  activeController = this;
}

void AppController::setBootScreenActive(bool active) {
  bootScreenActive_ = active;
}

void AppController::applyConfig(const AppControllerConfig& config) {
  config_ = config;
  noteActivity();
  markUiDirty();
}

void AppController::setSdMounted(bool mounted) {
  state_.sdMounted = mounted;
  refreshSdStats(true);
  markUiDirty();
}

void AppController::setWifiConfigured(bool configured) {
  config_.wifiConfigured = configured;
  markUiDirty();
}

void AppController::setStorageConfigStatus(bool wifiFromSd, bool batteryCurveFromSd, bool messagesRestoredFromSd) {
  state_.wifiConfigFromSd = wifiFromSd;
  state_.batteryCurveFromSd = batteryCurveFromSd;
  state_.messagesRestoredFromSd = messagesRestoredFromSd;
  markUiDirty();
}

bool AppController::sdMounted() const {
  return state_.sdMounted;
}

bool AppController::ntpSynced() const {
  return state_.ntpSynced;
}

void AppController::readSensors(bool force) {
  const BatteryStatus previousBattery = state_.battery;
  const Shtc3Reading previousEnvironment = state_.environment;
  const int previousEtaMinutes = batteryRuntime_.etaMinutes();

  BatteryStatus nextBattery = battery_.readStatus();
  Shtc3Reading nextEnvironment;
  shtc3_.read(nextEnvironment);
  state_.battery = nextBattery;
  state_.environment = nextEnvironment;
  readRtc(force);
  batteryRuntime_.updateEstimate(state_.battery, millis() / 1000UL, config_.batteryLogIntervalMs);

  const uint32_t nowMs = millis();
  if (verifySdMounted() && storage_.isReady() &&
      (force || state_.lastBatteryLogMs == 0 || nowMs - state_.lastBatteryLogMs >= config_.batteryLogIntervalMs)) {
    if (storage_.appendBatterySample(state_.battery, state_.now, nowMs / 1000UL)) {
      state_.lastBatteryLogMs = nowMs;
      appendBatteryChartSample(state_.battery);
    }
  } else if (!storage_.isReady()) {
    appendBatteryChartSample(state_.battery);
  }

  if (force ||
      batteryDisplayChanged(previousBattery, state_.battery) ||
      environmentDisplayChanged(previousEnvironment, state_.environment) ||
      previousEtaMinutes != batteryRuntime_.etaMinutes()) {
    markUiDirty();
  }
}

void AppController::readRtc(bool force) {
  const uint32_t now = millis();
  if (force || now - state_.lastRtcMs >= config_.rtcPollMs) {
    RtcDateTime dt;
    rtc_.read(dt);
    if (dt.second != state_.now.second || dt.valid != state_.now.valid || force) {
      markUiDirty();
    }
    state_.now = dt;
    state_.lastRtcMs = now;
  }
}

bool AppController::trySyncTime(bool force) {
  const uint32_t now = millis();
  if (!config_.wifiConfigured || !wifi_.isConnected()) {
    if (force) {
      Serial.println(config_.wifiConfigured ? "NTP: skipped, wifi offline" : "NTP: skipped, wifi not configured");
      state_.ntpSyncFailed = true;
      markUiDirty();
    }
    return false;
  }

  const uint32_t retryMs = state_.ntpSynced ? config_.ntpRetryMs : config_.ntpUnsyncedRetryMs;
  if (!force && now - state_.lastNtpAttemptMs < retryMs) {
    return state_.ntpSynced;
  }

  state_.lastNtpAttemptMs = now;
  if (!timeSync_.begin(config_.timezone)) {
    state_.ntpSyncFailed = true;
    markUiDirty();
    return false;
  }

  state_.ntpSyncing = true;
  state_.ntpSyncFailed = false;
  markUiDirty();
  if (display_.isReady() && !bootScreenActive_) {
    renderUi();
  }

  state_.ntpSynced = timeSync_.syncToRtc(rtc_, 12000);
  state_.ntpSyncing = false;
  state_.ntpSyncFailed = !state_.ntpSynced;
  rtc_.read(state_.now);
  markUiDirty();
  return state_.ntpSynced;
}

bool AppController::runInitialNtpSyncStep() {
  if (!state_.initialNtpSyncPending || bootScreenActive_) {
    return false;
  }
  if (!config_.wifiConfigured) {
    state_.initialNtpSyncPending = false;
    return false;
  }
  if (!wifi_.isConnected()) {
    return false;
  }

  state_.initialNtpSyncPending = false;
  trySyncTime(true);
  return true;
}

void AppController::makeBootIdFromCurrentTime() {
  state_.bootId = makeBootId(state_.now);
}

void AppController::renderUi() {
  ui_.render(buildUiModel());
  state_.uiDirty = false;
}

void AppController::syncHubTelemetry(bool force) {
  if (!state_.now.valid) {
    Serial.println("Hub: telemetry skipped, rtc invalid");
    return;
  }

  const HubRequestResult result =
      hub_.syncTelemetry(buildHubTelemetrySnapshot(), force, wifi_.isConnected(), handleHubStateChanged);
  if (result.attempted) {
    markUiDirty();
  }
}

void AppController::pollHubMessages(bool force) {
  const size_t before = hub_.messageCount();
  const HubRequestResult result = hub_.pollMessages(force, wifi_.isConnected(), handleHubStateChanged);
  if (!result.attempted) {
    return;
  }

  if (hub_.messageCount() != before) {
    state_.selectedMessage = 0;
    state_.messageBodyScrollLine = 0;
    if (verifySdMounted()) {
      storage_.saveMessages(hub_.messages(), hub_.messageCount());
    }
    if (state_.page != DesktopClockPage::Message) {
      state_.pendingNewMessageAlert = true;
    }
  }
  markUiDirty();
}

void AppController::pollWeather(bool force) {
  const HubRequestResult result = hub_.pollWeather(force, wifi_.isConnected(), handleHubStateChanged);
  if (result.attempted) {
    if (result.ok) {
      if (verifySdMounted()) {
        storage_.saveWeather(hub_.weather());
      }
    }
    markUiDirty();
  }
}

void AppController::pollTodos(bool force) {
  const HubRequestResult result = hub_.pollTodos(force, wifi_.isConnected(), handleHubStateChanged);
  if (result.attempted) {
    if (result.ok) {
      if (verifySdMounted()) {
        storage_.saveTodos(hub_.todos(), hub_.todoCount());
      }
    }
    updateSelectedTodoAfterChange();
    markUiDirty();
  }
}

void AppController::restoreBatteryHistoryFromStorage() {
  if (!verifySdMounted() || !storage_.isReady()) {
    return;
  }

  StoredBatteryHistoryPoint points[AppStorage::MaxBatteryHistoryPoints];
  size_t count = 0;
  if (!storage_.loadBatteryHistory(points, AppStorage::MaxBatteryHistoryPoints, count) || count == 0) {
    return;
  }

  batteryRuntime_.restoreChart(points, count);
  markUiDirty();
}

void AppController::loopOnce() {
  if (state_.lastActivityMs == 0) {
    state_.lastActivityMs = millis();
  }
  updateCpuFrequency();
  if (state_.serialCanvasMode) {
    handleSerialCanvasMode();
    updateCpuFrequency();
    delay(1);
    return;
  }
  handleButtons();
  handleMessageDeleteHold();
  handlePendingKeyClick();
  handleWifi();
  readRtc(false);
  const bool didInitialNtpWork = runInitialNtpSyncStep();
  runScheduledTasks(false);
  const bool didInitialHubWork = !didInitialNtpWork && runNextInitialHubSyncStep();
  if (!didInitialNtpWork && !didInitialHubWork) {
    runNextScheduledTask();
  }

  if (hub_.update()) {
    markUiDirty();
  }

  if (state_.scheduledTaskStep == State::ScheduledTaskStep::Idle &&
      state_.initialHubSyncStep == State::InitialHubSyncStep::Done) {
    publishPendingNewMessageAlert();
  }

  if (state_.newMessageAlert) {
    const uint32_t now = millis();
    if (now - state_.lastAlertBlinkMs >= config_.newMessageBlinkMs) {
      state_.lastAlertBlinkMs = now;
      markUiDirty();
    }
  }

  if (state_.uiDirty) {
    renderUi();
  }

  updateCpuFrequency();
  delay(config_.loopDelayMs);
}

void AppController::handleHubStateChanged() {
  if (activeController) {
    activeController->renderHubState();
  }
}

String AppController::formatRtcTimestamp(const RtcDateTime& dt) const {
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

String AppController::makeBootId(const RtcDateTime& dt) const {
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

DesktopClockUiModel AppController::buildUiModel() const {
  DesktopClockUiModel model;
  model.page = state_.page;
  model.battery = state_.battery;
  model.environment = state_.environment;
  model.now = state_.now;
  model.ntpSynced = state_.ntpSynced;
  model.ntpSyncing = state_.ntpSyncing;
  model.ntpSyncFailed = state_.ntpSyncFailed;
  model.hubSyncing = hub_.isSyncing();
  model.hubSyncFailed = hub_.hasFailed();
  model.hubConfigured = hub_.isConfigured();
  model.hubBound = hub_.isBound();
  model.hubDeviceId = hub_.deviceId();
  model.hubBindCode = hub_.bindCode();
  model.hubDeviceName = hub_.deviceName();
  model.sdMounted = state_.sdMounted;
  model.sdStatus = sdCard_.lastErrorText();
  model.storageReady = storage_.isReady();
  model.sdCardTotalMb = state_.sdCardTotalMb;
  model.sdCardUsedMb = state_.sdCardUsedMb;
  model.wifiConfigured = config_.wifiConfigured;
  model.wifiConfigFromSd = state_.wifiConfigFromSd;
  model.batteryCurveFromSd = state_.batteryCurveFromSd;
  model.messagesRestoredFromSd = state_.messagesRestoredFromSd;
  model.batteryLogIntervalMs = config_.batteryLogIntervalMs;
  model.selectedSystemMenuItem = state_.selectedSystemMenuItem;
  model.selectedSystemAction = state_.selectedSystemAction;
  model.systemActionFocused = state_.systemActionFocused;
  model.wifiConnected = wifi_.isConnected();
  model.wifiDisabled = state_.wifiDisabled;
  model.wifiAutoDisabled = state_.wifiAutoDisabled;
  model.wifiFailureCount = state_.wifiFailureCount;
  model.wifiMaxFailures = config_.wifiMaxFailures;
  model.wifiRssi = wifi_.rssi();
  model.wifiIp = wifi_.isConnected() ? wifi_.ipAddress() : "";
  model.wifiSsid = wifi_.ssid();
  model.uptimeMs = millis();
  model.freeHeap = ESP.getFreeHeap();
  model.heapSize = ESP.getHeapSize();
  model.freePsram = ESP.getFreePsram();
  model.psramSize = ESP.getPsramSize();
  model.cpuMhz = getCpuFrequencyMhz();
  model.batteryEtaMinutes = batteryRuntime_.etaMinutes();
  batteryRuntime_.fillUiModel(model);
  model.newMessageAlert = state_.newMessageAlert;
  model.newMessageAlertInvert =
      state_.newMessageAlert && ((millis() / config_.newMessageBlinkMs) % 2 == 1);
  model.weather = hub_.weather();
  model.messages = hub_.messages();
  model.messageCount = hub_.messageCount();
  model.selectedMessage = state_.selectedMessage;
  model.messageBodyFocused = state_.messageBodyFocused;
  model.messageBodyScrollLine = state_.messageBodyScrollLine;
  model.messageDeleteProgress = state_.messageDeleteProgress;
  model.todos = hub_.todos();
  model.todoCount = hub_.todoCount();
  model.selectedTodo = state_.selectedTodo;
  return model;
}

void AppController::renderHubState() {
  markUiDirty();
  if (display_.isReady() && !bootScreenActive_) {
    renderUi();
  }
}

void AppController::handleWifi() {
  const uint32_t now = millis();
  if (!config_.wifiConfigured || state_.wifiDisabled || wifi_.isConnected() ||
      now - state_.lastWifiRetryMs < config_.wifiRetryMs) {
    return;
  }

  if (wifi_.connect(5000)) {
    state_.wifiFailureCount = 0;
    state_.wifiAutoDisabled = false;
  } else if (state_.wifiFailureCount < 255) {
    ++state_.wifiFailureCount;
    if (config_.wifiMaxFailures > 0 && state_.wifiFailureCount >= config_.wifiMaxFailures) {
      state_.wifiDisabled = true;
      state_.wifiAutoDisabled = true;
      wifi_.disconnect(true);
      Serial.println("WiFi: auto disabled after repeated failures");
    }
  }
  state_.lastWifiRetryMs = now;
  markUiDirty();
}

void AppController::runScheduledTasks(bool force, bool includeTelemetry) {
  const uint32_t now = millis();
  if (state_.scheduledTaskStep != State::ScheduledTaskStep::Idle) {
    if (force) {
      queueScheduledTasks(true, includeTelemetry);
    }
    return;
  }
  if (!force && state_.lastHubSyncWindowMs == 0) {
    state_.lastHubSyncWindowMs = now;
    return;
  }
  if (!force && state_.lastHubSyncWindowMs != 0 && now - state_.lastHubSyncWindowMs < config_.hubSyncWindowMs) {
    return;
  }

  state_.lastHubSyncWindowMs = now;
  bool telemetryDue = includeTelemetry;
  const uint8_t telemetryWindows = config_.telemetryEveryHubSyncWindows;
  if (!telemetryDue && telemetryWindows > 0) {
    ++state_.hubSyncWindowCount;
    if (state_.hubSyncWindowCount >= telemetryWindows) {
      state_.hubSyncWindowCount = 0;
      telemetryDue = true;
    }
  }

  queueScheduledTasks(force, telemetryDue);
}

bool AppController::runNextInitialHubSyncStep() {
  if (bootScreenActive_ || state_.initialHubSyncStep == State::InitialHubSyncStep::Done) {
    return false;
  }
  if (!hub_.isConfigured()) {
    state_.initialHubSyncStep = State::InitialHubSyncStep::Done;
    publishPendingNewMessageAlert();
    return false;
  }
  if (!hubRequestsReady()) {
    return false;
  }

  switch (state_.initialHubSyncStep) {
    case State::InitialHubSyncStep::Telemetry:
      syncHubTelemetry(true);
      state_.initialHubSyncStep = State::InitialHubSyncStep::Weather;
      return true;
    case State::InitialHubSyncStep::Weather:
      pollWeather(true);
      state_.initialHubSyncStep = State::InitialHubSyncStep::Messages;
      return true;
    case State::InitialHubSyncStep::Messages:
      pollHubMessages(true);
      state_.initialHubSyncStep = State::InitialHubSyncStep::Todos;
      return true;
    case State::InitialHubSyncStep::Todos:
      pollTodos(true);
      state_.initialHubSyncStep = State::InitialHubSyncStep::Done;
      publishPendingNewMessageAlert();
      return true;
    case State::InitialHubSyncStep::Done:
      break;
  }
  return false;
}

bool AppController::runNextScheduledTask() {
  switch (state_.scheduledTaskStep) {
    case State::ScheduledTaskStep::Idle:
      return false;
    case State::ScheduledTaskStep::WifiSignal:
      wifi_.updateSignal();
      state_.scheduledTaskStep = State::ScheduledTaskStep::Ntp;
      return true;
    case State::ScheduledTaskStep::Ntp:
      trySyncTime(state_.scheduledTaskForce);
      state_.scheduledTaskStep = State::ScheduledTaskStep::Sensors;
      return true;
    case State::ScheduledTaskStep::Sensors:
      readSensors(true);
      state_.scheduledTaskStep = State::ScheduledTaskStep::Messages;
      return true;
    case State::ScheduledTaskStep::Messages:
      if (hubRequestsReady()) {
        pollHubMessages(state_.scheduledTaskForce);
      }
      state_.scheduledTaskStep = State::ScheduledTaskStep::TodoSync;
      return true;
    case State::ScheduledTaskStep::TodoSync: {
      HubRequestResult todoSync = hubRequestsReady()
                                      ? hub_.syncTodoChanges(true, handleHubStateChanged)
                                      : HubRequestResult{};
      state_.scheduledTaskTodoSyncOk = !todoSync.attempted || todoSync.ok;
      if (todoSync.attempted) {
        updateSelectedTodoAfterChange();
        markUiDirty();
      }
      state_.scheduledTaskStep =
          state_.scheduledTaskTodoSyncOk ? State::ScheduledTaskStep::Todos : State::ScheduledTaskStep::Weather;
      return true;
    }
    case State::ScheduledTaskStep::Todos:
      if (hubRequestsReady()) {
        pollTodos(state_.scheduledTaskForce);
      }
      state_.scheduledTaskStep = State::ScheduledTaskStep::Weather;
      return true;
    case State::ScheduledTaskStep::Weather:
      if (hubRequestsReady()) {
        pollWeather(state_.scheduledTaskForce);
      }
      if (state_.scheduledTaskIncludeTelemetry) {
        state_.scheduledTaskStep = State::ScheduledTaskStep::Telemetry;
      } else {
        state_.scheduledTaskStep = State::ScheduledTaskStep::Idle;
        state_.scheduledTaskForce = false;
        state_.scheduledTaskIncludeTelemetry = false;
        state_.scheduledTaskTodoSyncOk = true;
        publishPendingNewMessageAlert();
      }
      return true;
    case State::ScheduledTaskStep::Telemetry:
      if (hubRequestsReady()) {
        syncHubTelemetry(true);
      }
      state_.scheduledTaskStep = State::ScheduledTaskStep::Idle;
      state_.scheduledTaskForce = false;
      state_.scheduledTaskIncludeTelemetry = false;
      state_.scheduledTaskTodoSyncOk = true;
      publishPendingNewMessageAlert();
      return true;
  }
  return false;
}

void AppController::queueScheduledTasks(bool force, bool includeTelemetry) {
  noteActivity();
  state_.scheduledTaskForce = force;
  state_.scheduledTaskIncludeTelemetry = includeTelemetry;
  state_.scheduledTaskTodoSyncOk = true;
  state_.scheduledTaskStep = State::ScheduledTaskStep::WifiSignal;
  markUiDirty();
}

bool AppController::hubRequestsReady() const {
  return hub_.isConfigured() && wifi_.isConnected();
}

void AppController::publishPendingNewMessageAlert() {
  if (!state_.pendingNewMessageAlert) {
    return;
  }
  if (state_.page == DesktopClockPage::Message) {
    state_.pendingNewMessageAlert = false;
    return;
  }
  if (bootScreenActive_) {
    return;
  }

  state_.pendingNewMessageAlert = false;
  state_.newMessageAlert = true;
  state_.lastAlertBlinkMs = millis();
  markUiDirty();
}

bool AppController::verifySdMounted() {
  if (!sdCard_.isMounted()) {
    return false;
  }
  if (sdCard_.verifyMounted()) {
    return true;
  }

  state_.sdMounted = false;
  state_.sdCardTotalMb = 0;
  state_.sdCardUsedMb = 0;
  markUiDirty();
  Serial.println("SD: card removed");
  return false;
}

void AppController::refreshSdStats(bool force) {
  const uint32_t now = millis();
  if (!force && now - state_.lastSdStatsMs < config_.sdStatsRefreshMs) {
    return;
  }

  state_.lastSdStatsMs = now;
  if (!verifySdMounted()) {
    state_.sdCardTotalMb = 0;
    state_.sdCardUsedMb = 0;
    markUiDirty();
    return;
  }

  state_.sdCardTotalMb = sdCard_.cardSizeBytes() / (1024UL * 1024UL);
  state_.sdCardUsedMb = sdCard_.usedBytes() / (1024UL * 1024UL);
  markUiDirty();
}

void AppController::handleForcedRefresh() {
  if (!sdCard_.isMounted()) {
    setSdMounted(sdCard_.begin());
    if (sdCard_.isMounted()) {
      storage_.begin(sdCard_);
    }
    sdCard_.printInfo(Serial);
  }
  refreshSdStats(true);
  if (!wifi_.isConnected() && config_.wifiConfigured && !state_.wifiDisabled) {
    if (wifi_.connect(8000)) {
      state_.wifiFailureCount = 0;
      state_.wifiAutoDisabled = false;
    } else if (state_.wifiFailureCount < 255) {
      ++state_.wifiFailureCount;
      if (config_.wifiMaxFailures > 0 && state_.wifiFailureCount >= config_.wifiMaxFailures) {
        state_.wifiDisabled = true;
        state_.wifiAutoDisabled = true;
        wifi_.disconnect(true);
        Serial.println("WiFi: auto disabled after repeated failures");
      }
    }
  }
  queueScheduledTasks(true, true);
}

void AppController::updateSelectedTodoAfterChange() {
  const size_t count = hub_.todoCount();
  if (count == 0) {
    state_.selectedTodo = 0;
  } else if (state_.selectedTodo >= count) {
    state_.selectedTodo = count - 1;
  }
}

void AppController::appendBatteryChartSample(const BatteryStatus& battery) {
  if (batteryRuntime_.appendChartSample(battery, state_.now, millis(), config_.batteryLogIntervalMs)) {
    markUiDirty();
  }
}

void AppController::resetBatteryWindow() {
  batteryRuntime_.reset(state_.battery.charging);

  if (verifySdMounted() && storage_.isReady()) {
    BatteryStatus resetMarker = state_.battery;
    resetMarker.charging = true;
    const uint32_t uptimeS = millis() / 1000UL;
    storage_.appendBatterySample(resetMarker, state_.now, uptimeS);
    if (storage_.appendBatterySample(state_.battery, state_.now, uptimeS)) {
      state_.lastBatteryLogMs = millis();
    }
  }

  appendBatteryChartSample(state_.battery);
  markUiDirty();
}


HubTelemetrySnapshot AppController::buildHubTelemetrySnapshot() const {
  HubTelemetrySnapshot snapshot;
  snapshot.deviceId = config_.deviceId;
  snapshot.bootId = state_.bootId;
  snapshot.reportTimestamp = formatRtcTimestamp(state_.now);
  snapshot.uptimeS = millis() / 1000UL;

  snapshot.battery = state_.battery;
  snapshot.usbConnected = state_.battery.charging;

  snapshot.environment = state_.environment;

  snapshot.wifiConnected = wifi_.isConnected();
  snapshot.wifiSsid = wifi_.ssid();
  snapshot.wifiRssiDbm = wifi_.rssi();
  snapshot.wifiIp = wifi_.ipAddress();

  snapshot.freeHeapBytes = ESP.getFreeHeap();
  snapshot.freePsramBytes = ESP.getFreePsram();
  snapshot.ntpSync = state_.ntpSynced;

  snapshot.sdCardPresent = sdCard_.isMounted();
  snapshot.sdCardTotalMb = state_.sdCardTotalMb;
  snapshot.sdCardUsedMb = state_.sdCardUsedMb;
  return snapshot;
}

uint16_t AppController::messageBodyLineCount(const String& text) const {
  return Utf8Text::wrappedLineCount(text, 236);
}

void AppController::handleMessageKeyClick() {
  const size_t count = hub_.messageCount();
  if (count == 0) {
    return;
  }

  if (!state_.messageBodyFocused) {
    state_.selectedMessage = (state_.selectedMessage + 1) % count;
    state_.messageBodyScrollLine = 0;
    markUiDirty();
    return;
  }

  const HubMessage* message = hub_.messageAt(state_.selectedMessage);
  const uint16_t lineCount = message ? messageBodyLineCount(message->body) : 1;
  constexpr uint16_t kPageLines = 11;
  if (lineCount <= kPageLines || state_.messageBodyScrollLine + kPageLines >= lineCount) {
    state_.messageBodyScrollLine = 0;
  } else {
    state_.messageBodyScrollLine += kPageLines;
  }
  markUiDirty();
}

void AppController::handleMessageDelete() {
  const size_t count = hub_.messageCount();
  if (count == 0 || state_.messageBodyFocused) {
    return;
  }

  const size_t index = min(state_.selectedMessage, count - 1);
  if (!hub_.deleteMessageLocal(index)) {
    return;
  }

  if (verifySdMounted()) {
    storage_.saveMessages(hub_.messages(), hub_.messageCount());
  }
  const size_t nextCount = hub_.messageCount();
  if (nextCount == 0) {
    state_.selectedMessage = 0;
    state_.messageBodyScrollLine = 0;
  } else if (state_.selectedMessage >= nextCount) {
    state_.selectedMessage = nextCount - 1;
  }
  state_.messageDeleteProgress = 0;
  state_.messageDeleteTriggered = true;
  markUiDirty();
}

void AppController::handleMessageDeleteHold() {
  if (state_.page != DesktopClockPage::Message || state_.messageBodyFocused || hub_.messageCount() == 0 ||
      !keyButton_.isPressed() || state_.messageDeleteTriggered) {
    if (state_.messageDeleteProgress != 0) {
      state_.messageDeleteProgress = 0;
      markUiDirty();
    }
    return;
  }

  const uint32_t heldMs = keyButton_.currentPressDurationMs();
  const uint8_t progress = heldMs < kMessageDeleteProgressShowMs
                               ? 0
                               : static_cast<uint8_t>(min<uint32_t>(100, heldMs * 100UL / config_.keyLongPressMs));
  if (progress != state_.messageDeleteProgress) {
    state_.messageDeleteProgress = progress;
    markUiDirty();
  }
  if (heldMs >= config_.keyLongPressMs) {
    handleMessageDelete();
  }
}

void AppController::handleTodoKeyClick() {
  const size_t count = hub_.todoCount();
  if (count == 0) {
    return;
  }

  state_.selectedTodo = (state_.selectedTodo + 1) % count;
  markUiDirty();
}

void AppController::handleTodoStatusToggle() {
  const HubTodo* todo = hub_.todoAt(state_.selectedTodo);
  if (!todo) {
    return;
  }

  const int nextStatus = (todo->status + 1) % 3;
  if (hub_.setTodoStatusLocal(state_.selectedTodo, nextStatus)) {
    markUiDirty();
  }
}

void AppController::handleTodoDelete() {
  const HubTodo* todo = hub_.todoAt(state_.selectedTodo);
  if (!todo) {
    return;
  }

  if (hub_.deleteTodoLocal(state_.selectedTodo)) {
    const size_t count = hub_.todoCount();
    if (count == 0) {
      state_.selectedTodo = 0;
    } else if (state_.selectedTodo >= count) {
      state_.selectedTodo = count - 1;
    }
    markUiDirty();
  }
}

void AppController::handleSystemKeyClick() {
  if (state_.selectedSystemMenuItem == kSystemMenuAction && state_.systemActionFocused) {
    state_.selectedSystemAction = (state_.selectedSystemAction + 1) % kSystemActionCount;
    markUiDirty();
    Serial.println("KEY: system action button");
    return;
  }

  state_.selectedSystemMenuItem = (state_.selectedSystemMenuItem + 1) % kSystemMenuItemCount;
  state_.systemActionFocused = false;
  if (state_.selectedSystemMenuItem == kSystemMenuStorage) {
    refreshSdStats(true);
  }
  markUiDirty();
  Serial.println("KEY: system menu");
}

void AppController::handleSystemAction() {
  if (state_.selectedSystemMenuItem == kSystemMenuAction) {
    if (!state_.systemActionFocused) {
      state_.systemActionFocused = true;
      state_.selectedSystemAction = 0;
      markUiDirty();
      Serial.println("KEY: system action focus");
      return;
    }
    state_.systemActionFocused = false;
    markUiDirty();
    Serial.println("KEY: system action menu");
    return;
  }

  Serial.println("KEY: system action ignored");
}

void AppController::handleSystemClearMessages() {
  if (hub_.messageCount() == 0) {
    Serial.println("KEY: system action ignored");
    return;
  }

  hub_.clearMessagesLocal();
  if (verifySdMounted()) {
    storage_.saveMessages(hub_.messages(), hub_.messageCount());
  }
  state_.selectedMessage = 0;
  state_.messageBodyScrollLine = 0;
  state_.newMessageAlert = false;
  state_.pendingNewMessageAlert = false;
  markUiDirty();
  Serial.println("KEY: clear messages");
}

void AppController::handleSystemWifiToggle() {
  if (!config_.wifiConfigured) {
    Serial.println("KEY: wifi not configured");
    return;
  }

  if (!state_.wifiDisabled || wifi_.isConnected()) {
    wifi_.disconnect(true);
    state_.wifiDisabled = true;
    state_.wifiAutoDisabled = false;
    markUiDirty();
    Serial.println("KEY: wifi off");
    return;
  }

  state_.wifiDisabled = false;
  state_.wifiAutoDisabled = false;
  state_.wifiFailureCount = 0;
  state_.lastWifiRetryMs = 0;
  if (wifi_.connect(8000)) {
    Serial.println("KEY: wifi on");
  } else {
    state_.wifiFailureCount = 1;
    Serial.println("KEY: wifi on failed");
  }
  markUiDirty();
}

void AppController::handleSystemResetBattery() {
  resetBatteryWindow();
  Serial.println("KEY: reset battery window");
}

void AppController::handleSystemRePair() {
  hub_.setDeviceSecret("");
  hub_.setDeviceBinding(false, "", "");

  if (storage_.isReady()) {
    StoredHubCredentials cleared;
    storage_.saveHubCredentials(cleared);
  }

  markUiDirty();
  if (!wifi_.isConnected()) {
    Serial.println("KEY: re-pair pending (wifi offline)");
    return;
  }

  HubHelloResult hello = hub_.hello(true, handleHubStateChanged);
  if (hello.attempted && hello.ok) {
    StoredHubCredentials updated;
    updated.bound = hello.bound;
    snprintf(updated.deviceSecret, sizeof(updated.deviceSecret), "%s", hello.deviceSecret.c_str());
    snprintf(updated.bindCode, sizeof(updated.bindCode), "%s", hello.bindCode.c_str());
    snprintf(updated.deviceName, sizeof(updated.deviceName), "%s", hello.name.c_str());
    if (storage_.isReady()) {
      storage_.saveHubCredentials(updated);
    }
  }

  markUiDirty();
}

void AppController::enterSerialCanvasMode() {
  state_.serialCanvasMode = true;
  state_.serialCanvasFlushPending = false;
  state_.serialCanvasLineLen = 0;
  state_.lastSerialCanvasFlushMs = 0;
  state_.lastSerialCanvasInputMs = millis();
  state_.pendingKeyClick = false;
  state_.systemActionFocused = false;
  state_.uiDirty = false;
  noteActivity();

  display_.clear(true);
  display_.drawText(22, 48, "SERIAL CANVAS", true, 3);
  display_.drawText(24, 96, "Send drawing commands over USB CDC", true, 1);
  display_.drawText(24, 116, "HELP for protocol", true, 1);
  display_.drawText(24, 136, "EXIT or long key to leave", true, 1);
  display_.drawText(24, 170, "Default flush limit: 5 Hz", true, 1);
  requestSerialCanvasFlush(true);

  Serial.println("CANVAS READY TPD1");
  Serial.println("CANVAS HELP: CLEAR|PX|LINE|RECT|FILL|CIRCLE|TEXT|FLUSH|EXIT");
}

void AppController::exitSerialCanvasMode() {
  if (!state_.serialCanvasMode) {
    return;
  }

  state_.serialCanvasMode = false;
  state_.serialCanvasFlushPending = false;
  state_.serialCanvasLineLen = 0;
  state_.pendingKeyClick = false;
  markUiDirty();
  renderUi();
  Serial.println("CANVAS EXIT");
}

void AppController::handleSerialCanvasMode() {
  keyButton_.update();
  bootButton_.update();

  if (keyButton_.consumeReleased() || bootButton_.consumeReleased()) {
    noteActivity();
    const bool keyLongPress = keyButton_.lastPressDurationMs() >= config_.keyLongPressMs;
    const bool bootLongPress = bootButton_.lastPressDurationMs() >= config_.keyLongPressMs;
    if (keyLongPress || bootLongPress) {
      exitSerialCanvasMode();
      return;
    }
  }

  processSerialCanvasInput();
  if (state_.serialCanvasFlushPending &&
      millis() - state_.lastSerialCanvasInputMs >= kSerialCanvasIdleFlushMs) {
    requestSerialCanvasFlush(true);
  }
}

void AppController::processSerialCanvasInput() {
  while (Serial.available() > 0) {
    state_.lastSerialCanvasInputMs = millis();
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      state_.serialCanvasLine[state_.serialCanvasLineLen] = '\0';
      if (state_.serialCanvasLineLen > 0) {
        processSerialCanvasLine(state_.serialCanvasLine);
      }
      state_.serialCanvasLineLen = 0;
      if (!state_.serialCanvasMode) {
        return;
      }
      continue;
    }
    if (state_.serialCanvasLineLen + 1 >= sizeof(state_.serialCanvasLine)) {
      state_.serialCanvasLineLen = 0;
      Serial.println("CANVAS ERR line-too-long");
      continue;
    }
    state_.serialCanvasLine[state_.serialCanvasLineLen++] = ch;
  }
}

void AppController::processSerialCanvasLine(char* line) {
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  if (*line == '\0' || *line == '#') {
    return;
  }

  char* save = nullptr;
  char* command = strtok_r(line, " \t", &save);
  if (command == nullptr) {
    return;
  }

  if (strcasecmp(command, "HELP") == 0) {
    Serial.println("CANVAS CMDS:");
    Serial.println("CLEAR W|B");
    Serial.println("PX x y color");
    Serial.println("LINE x0 y0 x1 y1 color");
    Serial.println("RECT x y w h color");
    Serial.println("FILL x y w h color");
    Serial.println("CIRCLE x y r color");
    Serial.println("TEXT x y scale color text");
    Serial.println("FLUSH");
    Serial.println("EXIT");
    return;
  }

  if (strcasecmp(command, "PING") == 0) {
    Serial.println("CANVAS PONG");
    display_.fillRect(300, 260, 72, 18, false);
    display_.drawText(304, 264, "PING", true, 1);
    requestSerialCanvasFlush(true);
    return;
  }

  if (strcasecmp(command, "EXIT") == 0) {
    exitSerialCanvasMode();
    return;
  }

  if (strcasecmp(command, "FLUSH") == 0) {
    requestSerialCanvasFlush(true);
    return;
  }

  if (strcasecmp(command, "CLEAR") == 0) {
    bool black = false;
    const char* colorToken = strtok_r(nullptr, " \t", &save);
    if (colorToken != nullptr && !parseCanvasColor(colorToken, black)) {
      Serial.println("CANVAS ERR clear-color");
      return;
    }
    display_.clear(!black);
    requestSerialCanvasFlush(false);
    return;
  }

  int values[5] = {};
  bool black = true;

  if (strcasecmp(command, "PX") == 0 || strcasecmp(command, "PIXEL") == 0) {
    for (uint8_t i = 0; i < 2; ++i) {
      if (!parseCanvasInt(strtok_r(nullptr, " \t", &save), values[i])) {
        Serial.println("CANVAS ERR pixel-args");
        return;
      }
    }
    if (!parseCanvasColor(strtok_r(nullptr, " \t", &save), black)) {
      Serial.println("CANVAS ERR pixel-color");
      return;
    }
    display_.setPixel(values[0], values[1], black);
    requestSerialCanvasFlush(false);
    return;
  }

  if (strcasecmp(command, "LINE") == 0) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (!parseCanvasInt(strtok_r(nullptr, " \t", &save), values[i])) {
        Serial.println("CANVAS ERR line-args");
        return;
      }
    }
    if (!parseCanvasColor(strtok_r(nullptr, " \t", &save), black)) {
      Serial.println("CANVAS ERR line-color");
      return;
    }
    display_.drawLine(values[0], values[1], values[2], values[3], black);
    requestSerialCanvasFlush(false);
    return;
  }

  if (strcasecmp(command, "RECT") == 0 || strcasecmp(command, "FILL") == 0) {
    for (uint8_t i = 0; i < 4; ++i) {
      if (!parseCanvasInt(strtok_r(nullptr, " \t", &save), values[i])) {
        Serial.println("CANVAS ERR rect-args");
        return;
      }
    }
    if (!parseCanvasColor(strtok_r(nullptr, " \t", &save), black)) {
      Serial.println("CANVAS ERR rect-color");
      return;
    }
    if (strcasecmp(command, "FILL") == 0) {
      display_.fillRect(values[0], values[1], values[2], values[3], black);
    } else {
      display_.drawRect(values[0], values[1], values[2], values[3], black);
    }
    requestSerialCanvasFlush(false);
    return;
  }

  if (strcasecmp(command, "CIRCLE") == 0) {
    for (uint8_t i = 0; i < 3; ++i) {
      if (!parseCanvasInt(strtok_r(nullptr, " \t", &save), values[i])) {
        Serial.println("CANVAS ERR circle-args");
        return;
      }
    }
    if (!parseCanvasColor(strtok_r(nullptr, " \t", &save), black)) {
      Serial.println("CANVAS ERR circle-color");
      return;
    }
    display_.drawCircle(values[0], values[1], values[2], black);
    requestSerialCanvasFlush(false);
    return;
  }

  if (strcasecmp(command, "TEXT") == 0) {
    for (uint8_t i = 0; i < 3; ++i) {
      if (!parseCanvasInt(strtok_r(nullptr, " \t", &save), values[i])) {
        Serial.println("CANVAS ERR text-args");
        return;
      }
    }
    if (!parseCanvasColor(strtok_r(nullptr, " \t", &save), black)) {
      Serial.println("CANVAS ERR text-color");
      return;
    }
    if (save == nullptr) {
      Serial.println("CANVAS ERR text-body");
      return;
    }
    while (*save == ' ' || *save == '\t') {
      ++save;
    }
    display_.drawText(values[0], values[1], save, black, values[2]);
    requestSerialCanvasFlush(false);
    return;
  }

  Serial.println("CANVAS ERR unknown-command");
}

void AppController::requestSerialCanvasFlush(bool force) {
  const uint32_t now = millis();
  if (!force) {
    state_.serialCanvasFlushPending = true;
    return;
  }

  display_.flushFull();
  state_.lastSerialCanvasFlushMs = now;
  state_.serialCanvasFlushPending = false;
}

void AppController::handleSingleKeyClick() {
  if (state_.newMessageAlert) {
    state_.newMessageAlert = false;
    state_.pendingNewMessageAlert = false;
    state_.page = DesktopClockPage::Message;
    state_.messageBodyFocused = false;
    state_.selectedMessage = 0;
    state_.messageBodyScrollLine = 0;
    markUiDirty();
    Serial.println("KEY: open new message");
    return;
  }

  if (state_.page == DesktopClockPage::Message) {
    handleMessageKeyClick();
    return;
  }

  if (state_.page == DesktopClockPage::Todo) {
    handleTodoKeyClick();
    return;
  }

  if (state_.page != DesktopClockPage::System) {
    Serial.println("KEY: no action on this page");
    return;
  }

  handleSystemKeyClick();
}

void AppController::handleKeyDoubleClick() {
  if (state_.page == DesktopClockPage::Todo) {
    handleTodoStatusToggle();
    return;
  }

  if (state_.page == DesktopClockPage::System) {
    handleSystemAction();
    return;
  }

  if (state_.page != DesktopClockPage::Message) {
    Serial.println("KEY: double ignored on this page");
    return;
  }

  state_.messageBodyFocused = !state_.messageBodyFocused;
  markUiDirty();
  Serial.println(state_.messageBodyFocused ? "KEY: message body focus" : "KEY: message list focus");
}

void AppController::handlePendingKeyClick() {
  if (!state_.pendingKeyClick) {
    return;
  }
  if (millis() - state_.pendingKeyClickMs < config_.keyDoubleClickMs) {
    return;
  }

  state_.pendingKeyClick = false;
  handleSingleKeyClick();
}

void AppController::handleButtons() {
  keyButton_.update();
  bootButton_.update();

  if (keyButton_.consumeReleased()) {
    noteActivity();
    const uint32_t now = millis();
    const bool longPress = keyButton_.lastPressDurationMs() >= config_.keyLongPressMs;
    if (longPress) {
      state_.pendingKeyClick = false;
      if (state_.messageDeleteTriggered) {
        state_.messageDeleteTriggered = false;
        state_.messageDeleteProgress = 0;
        return;
      }
      if (state_.page == DesktopClockPage::Message) {
        Serial.println("KEY: delete message");
        handleMessageDelete();
        return;
      }
      if (state_.page == DesktopClockPage::Todo) {
        Serial.println("KEY: delete todo");
        handleTodoDelete();
        return;
      }
      if (state_.page == DesktopClockPage::System && state_.selectedSystemMenuItem == kSystemMenuAction &&
          state_.systemActionFocused) {
        if (state_.selectedSystemAction == kSystemActionWifiToggle) {
          Serial.println("KEY: wifi toggle");
          handleSystemWifiToggle();
          return;
        }
        if (state_.selectedSystemAction == kSystemActionSyncNow) {
          Serial.println("KEY: system refresh");
          handleForcedRefresh();
          markUiDirty();
          return;
        }
        if (state_.selectedSystemAction == kSystemActionClearMessages) {
          Serial.println("KEY: clear messages");
          handleSystemClearMessages();
          return;
        }
        if (state_.selectedSystemAction == kSystemActionResetBattery) {
          Serial.println("KEY: reset battery");
          handleSystemResetBattery();
          return;
        }
        if (state_.selectedSystemAction == kSystemActionRePair) {
          Serial.println("KEY: re-pair");
          handleSystemRePair();
          return;
        }
        if (state_.selectedSystemAction == kSystemActionCanvas) {
          Serial.println("KEY: serial canvas");
          enterSerialCanvasMode();
          return;
        }
        if (state_.selectedSystemAction == kSystemActionBack) {
          state_.systemActionFocused = false;
          markUiDirty();
          Serial.println("KEY: system action back");
          return;
        }
      }
      if (state_.page != DesktopClockPage::System) {
        Serial.println("KEY: long ignored on this page");
        return;
      }
      Serial.println("KEY: long ignored on system");
    } else if (state_.newMessageAlert) {
      state_.pendingKeyClick = false;
      handleSingleKeyClick();
    } else if (state_.pendingKeyClick && now - state_.pendingKeyClickMs < config_.keyDoubleClickMs) {
      state_.pendingKeyClick = false;
      handleKeyDoubleClick();
    } else {
      state_.messageDeleteTriggered = false;
      state_.messageDeleteProgress = 0;
      state_.pendingKeyClick = true;
      state_.pendingKeyClickMs = now;
    }
  }

  if (bootButton_.consumeReleased()) {
    noteActivity();
    state_.page = DesktopClockUi::nextPage(state_.page);
    state_.systemActionFocused = false;
    if (state_.page == DesktopClockPage::System && state_.selectedSystemMenuItem == 1) {
      refreshSdStats(true);
    }
    if (state_.page == DesktopClockPage::Message) {
      state_.newMessageAlert = false;
    }
    if (state_.page == DesktopClockPage::Todo) {
      const size_t count = hub_.todoCount();
      if (count == 0) {
        state_.selectedTodo = 0;
      } else if (state_.selectedTodo >= count) {
        state_.selectedTodo = count - 1;
      }
    }
    state_.pendingKeyClick = false;
    markUiDirty();
    Serial.println("BOOT: page switched");
  }
}

void AppController::noteActivity() {
  state_.lastActivityMs = millis();
  if (config_.enableDynamicCpuFrequency) {
    applyCpuFrequency(config_.activeCpuMhz);
  }
}

void AppController::updateCpuFrequency() {
  if (!config_.enableDynamicCpuFrequency) {
    return;
  }

  applyCpuFrequency(shouldUseActiveCpu() ? config_.activeCpuMhz : config_.idleCpuMhz);
}

void AppController::applyCpuFrequency(uint8_t mhz) {
  if (mhz == 0 || state_.currentCpuMhz == mhz) {
    return;
  }

  if (setCpuFrequencyMhz(mhz)) {
    state_.currentCpuMhz = mhz;
    Serial.printf("CPU: %u MHz\n", static_cast<unsigned>(mhz));
  } else {
    Serial.printf("CPU: set %u MHz failed\n", static_cast<unsigned>(mhz));
  }
}

bool AppController::shouldUseActiveCpu() const {
  const uint32_t now = millis();
  if (state_.lastActivityMs == 0 || now - state_.lastActivityMs < config_.cpuIdleAfterMs) {
    return true;
  }
  if (bootScreenActive_ || state_.uiDirty || hub_.isSyncing()) {
    return true;
  }
  if (state_.pendingKeyClick || keyButton_.isPressed() || bootButton_.isPressed()) {
    return true;
  }
  if (state_.initialNtpSyncPending && wifi_.isConnected()) {
    return true;
  }
  if (state_.initialHubSyncStep != State::InitialHubSyncStep::Done && hubRequestsReady()) {
    return true;
  }
  if (scheduledTaskNeedsActiveCpu()) {
    return true;
  }
  return false;
}

bool AppController::scheduledTaskNeedsActiveCpu() const {
  switch (state_.scheduledTaskStep) {
    case State::ScheduledTaskStep::Idle:
    case State::ScheduledTaskStep::WifiSignal:
    case State::ScheduledTaskStep::Sensors:
      return false;
    case State::ScheduledTaskStep::Ntp:
      return config_.wifiConfigured && wifi_.isConnected();
    case State::ScheduledTaskStep::Messages:
    case State::ScheduledTaskStep::TodoSync:
    case State::ScheduledTaskStep::Todos:
    case State::ScheduledTaskStep::Weather:
    case State::ScheduledTaskStep::Telemetry:
      return hubRequestsReady();
  }
  return false;
}

void AppController::markUiDirty() {
  state_.uiDirty = true;
}
