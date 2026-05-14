#include "AppController.h"

#include "Utf8Text.h"

namespace {

AppController* activeController = nullptr;

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
  state_.battery = battery_.readStatus();
  shtc3_.read(state_.environment);
  readRtc(force);
  updateBatteryRuntimeEstimate();
  const uint32_t nowMs = millis();
  if (storage_.isReady() &&
      (force || state_.lastBatteryLogMs == 0 || nowMs - state_.lastBatteryLogMs >= config_.batteryLogIntervalMs)) {
    if (storage_.appendBatterySample(state_.battery, state_.now, nowMs / 1000UL)) {
      state_.lastBatteryLogMs = nowMs;
    }
  }
  markUiDirty();
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

void AppController::makeBootIdFromCurrentTime() {
  state_.bootId = makeBootId(state_.now);
}

void AppController::renderUi() {
  ui_.render(buildUiModel());
  state_.uiDirty = false;
}

void AppController::syncHubTelemetry(bool force) {
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
    storage_.saveMessages(hub_.messages(), hub_.messageCount());
    if (state_.page != DesktopClockPage::Message) {
      state_.newMessageAlert = true;
    }
  }
  markUiDirty();
}

void AppController::pollWeather(bool force) {
  const HubRequestResult result = hub_.pollWeather(force, wifi_.isConnected(), handleHubStateChanged);
  if (result.attempted) {
    if (result.ok) {
      storage_.saveWeather(hub_.weather());
    }
    markUiDirty();
  }
}

void AppController::pollTodos(bool force) {
  const HubRequestResult result = hub_.pollTodos(force, wifi_.isConnected(), handleHubStateChanged);
  if (result.attempted) {
    if (result.ok) {
      storage_.saveTodos(hub_.todos(), hub_.todoCount());
    }
    updateSelectedTodoAfterChange();
    markUiDirty();
  }
}

void AppController::loopOnce() {
  handleButtons();
  handlePendingKeyClick();
  handleWifi();
  readRtc(false);
  refreshSdStats(false);
  runScheduledTasks(false);
  const bool didInitialHubWork = runNextInitialHubSyncStep();
  if (!didInitialHubWork) {
    runNextScheduledTask();
  }

  if (hub_.update()) {
    markUiDirty();
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
  model.wifiConnected = wifi_.isConnected();
  model.wifiRssi = wifi_.rssi();
  model.wifiIp = wifi_.isConnected() ? wifi_.ipAddress() : "";
  model.wifiSsid = wifi_.ssid();
  model.uptimeMs = millis();
  model.freeHeap = ESP.getFreeHeap();
  model.heapSize = ESP.getHeapSize();
  model.freePsram = ESP.getFreePsram();
  model.psramSize = ESP.getPsramSize();
  model.batteryEtaMinutes = state_.batteryEtaMinutes;
  model.newMessageAlert = state_.newMessageAlert;
  model.newMessageAlertInvert =
      state_.newMessageAlert && ((millis() / config_.newMessageBlinkMs) % 2 == 1);
  model.weather = hub_.weather();
  model.messages = hub_.messages();
  model.messageCount = hub_.messageCount();
  model.selectedMessage = state_.selectedMessage;
  model.messageBodyFocused = state_.messageBodyFocused;
  model.messageBodyScrollLine = state_.messageBodyScrollLine;
  model.todos = hub_.todos();
  model.todoCount = hub_.todoCount();
  model.selectedTodo = state_.selectedTodo;
  return model;
}

void AppController::renderHubState() {
  markUiDirty();
  if (display_.isReady()) {
    renderUi();
  }
}

void AppController::handleWifi() {
  const uint32_t now = millis();
  if (!config_.wifiConfigured || wifi_.isConnected() || now - state_.lastWifiRetryMs < config_.wifiRetryMs) {
    return;
  }

  wifi_.connect(5000);
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
      pollHubMessages(state_.scheduledTaskForce);
      state_.scheduledTaskStep = State::ScheduledTaskStep::TodoSync;
      return true;
    case State::ScheduledTaskStep::TodoSync: {
      HubRequestResult todoSync = hub_.syncTodoChanges(wifi_.isConnected(), handleHubStateChanged);
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
      pollTodos(state_.scheduledTaskForce);
      state_.scheduledTaskStep = State::ScheduledTaskStep::Weather;
      return true;
    case State::ScheduledTaskStep::Weather:
      pollWeather(state_.scheduledTaskForce);
      if (state_.scheduledTaskIncludeTelemetry) {
        state_.scheduledTaskStep = State::ScheduledTaskStep::Telemetry;
      } else {
        state_.scheduledTaskStep = State::ScheduledTaskStep::Idle;
        state_.scheduledTaskForce = false;
        state_.scheduledTaskIncludeTelemetry = false;
        state_.scheduledTaskTodoSyncOk = true;
      }
      return true;
    case State::ScheduledTaskStep::Telemetry:
      syncHubTelemetry(true);
      state_.scheduledTaskStep = State::ScheduledTaskStep::Idle;
      state_.scheduledTaskForce = false;
      state_.scheduledTaskIncludeTelemetry = false;
      state_.scheduledTaskTodoSyncOk = true;
      return true;
  }
  return false;
}

void AppController::queueScheduledTasks(bool force, bool includeTelemetry) {
  state_.scheduledTaskForce = force;
  state_.scheduledTaskIncludeTelemetry = includeTelemetry;
  state_.scheduledTaskTodoSyncOk = true;
  state_.scheduledTaskStep = State::ScheduledTaskStep::WifiSignal;
  markUiDirty();
}

void AppController::refreshSdStats(bool force) {
  const uint32_t now = millis();
  if (!force && now - state_.lastSdStatsMs < config_.sdStatsRefreshMs) {
    return;
  }

  state_.lastSdStatsMs = now;
  if (!sdCard_.isMounted()) {
    state_.sdCardTotalMb = 0;
    state_.sdCardUsedMb = 0;
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
  if (!wifi_.isConnected() && config_.wifiConfigured) {
    wifi_.connect(8000);
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

void AppController::updateBatteryRuntimeEstimate() {
  const uint32_t nowS = millis() / 1000UL;
  constexpr float kFilterAlpha = 0.25f;
  constexpr float kMaxSingleSampleDropPercent = 8.0f;
  constexpr float kMaxDischargeRecoveryPercent = 0.2f;
  constexpr float kMinSlopePercentPerHour = 0.25f;
  constexpr float kMaxSlopePercentPerHour = 80.0f;
  constexpr uint32_t kMinElapsedS = 15UL * 60UL;
  constexpr size_t kMinSamples = 12;
  constexpr float kMinEstimatedDropPercent = 1.5f;

  state_.batteryEtaMinutes = -1;
  if (state_.battery.charging || state_.battery.percentFloat <= 0.0f) {
    state_.batteryHistoryCount = 0;
    state_.batteryHistoryNext = 0;
    state_.hasBatteryEtaFilter = false;
    state_.batteryEtaWasCharging = state_.battery.charging;
    return;
  }

  if (state_.batteryEtaWasCharging) {
    state_.batteryHistoryCount = 0;
    state_.batteryHistoryNext = 0;
    state_.hasBatteryEtaFilter = false;
    state_.batteryEtaWasCharging = false;
  }

  float filteredPercent = state_.battery.percentFloat;
  if (state_.hasBatteryEtaFilter) {
    filteredPercent = state_.batteryEtaFilteredPercent +
                      kFilterAlpha * (state_.battery.percentFloat - state_.batteryEtaFilteredPercent);
    if (filteredPercent > state_.batteryEtaFilteredPercent + kMaxDischargeRecoveryPercent) {
      filteredPercent = state_.batteryEtaFilteredPercent + kMaxDischargeRecoveryPercent;
    }
  } else {
    state_.hasBatteryEtaFilter = true;
  }

  filteredPercent = constrain(filteredPercent, 0.0f, 100.0f);
  if (state_.batteryHistoryCount > 0) {
    const State::BatteryHistoryPoint& previous =
        state_.batteryHistory[(state_.batteryHistoryNext + State::BatteryHistorySize - 1) % State::BatteryHistorySize];
    if (previous.percent - filteredPercent > kMaxSingleSampleDropPercent) {
      state_.batteryHistoryCount = 0;
      state_.batteryHistoryNext = 0;
      state_.hasBatteryEtaFilter = false;
      return;
    }
  }
  state_.batteryEtaFilteredPercent = filteredPercent;

  state_.batteryHistory[state_.batteryHistoryNext].uptimeS = nowS;
  state_.batteryHistory[state_.batteryHistoryNext].percent = filteredPercent;
  state_.batteryHistoryNext = (state_.batteryHistoryNext + 1) % State::BatteryHistorySize;
  if (state_.batteryHistoryCount < State::BatteryHistorySize) {
    ++state_.batteryHistoryCount;
  }

  if (state_.batteryHistoryCount < kMinSamples) {
    return;
  }

  const size_t oldestIndex =
      state_.batteryHistoryCount < State::BatteryHistorySize ? 0 : state_.batteryHistoryNext;
  const State::BatteryHistoryPoint& oldest = state_.batteryHistory[oldestIndex];
  const State::BatteryHistoryPoint& newest =
      state_.batteryHistory[(state_.batteryHistoryNext + State::BatteryHistorySize - 1) % State::BatteryHistorySize];

  const uint32_t elapsedS = newest.uptimeS - oldest.uptimeS;
  if (elapsedS < kMinElapsedS) {
    return;
  }

  double sumT = 0.0;
  double sumP = 0.0;
  double sumTT = 0.0;
  double sumTP = 0.0;
  for (size_t i = 0; i < state_.batteryHistoryCount; ++i) {
    const size_t index = (oldestIndex + i) % State::BatteryHistorySize;
    const double t = static_cast<double>(state_.batteryHistory[index].uptimeS - oldest.uptimeS);
    const double p = static_cast<double>(state_.batteryHistory[index].percent);
    sumT += t;
    sumP += p;
    sumTT += t * t;
    sumTP += t * p;
  }

  const double n = static_cast<double>(state_.batteryHistoryCount);
  const double denominator = n * sumTT - sumT * sumT;
  if (denominator <= 0.0) {
    return;
  }

  const double slopePercentPerSecond = (n * sumTP - sumT * sumP) / denominator;
  const float dischargePercentPerHour = static_cast<float>(-slopePercentPerSecond * 3600.0);
  const float estimatedDropPercent = dischargePercentPerHour * elapsedS / 3600.0f;
  if (dischargePercentPerHour < kMinSlopePercentPerHour ||
      dischargePercentPerHour > kMaxSlopePercentPerHour ||
      estimatedDropPercent < kMinEstimatedDropPercent) {
    return;
  }

  const float etaMinutes = newest.percent / dischargePercentPerHour * 60.0f;
  if (etaMinutes > 0.0f && etaMinutes < 10000.0f) {
    state_.batteryEtaMinutes = static_cast<int>(etaMinutes + 0.5f);
  }
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
  constexpr uint8_t kSystemMenuItemCount = 3;
  state_.selectedSystemMenuItem = (state_.selectedSystemMenuItem + 1) % kSystemMenuItemCount;
  markUiDirty();
  Serial.println("KEY: system menu");
}

void AppController::handleSystemAction() {
  constexpr uint8_t kSystemMenuRefresh = 2;
  if (state_.selectedSystemMenuItem != kSystemMenuRefresh) {
    Serial.println("KEY: system action ignored");
    return;
  }

  Serial.println("KEY: system refresh");
  handleForcedRefresh();
  markUiDirty();
}

void AppController::handleSingleKeyClick() {
  if (state_.newMessageAlert) {
    state_.newMessageAlert = false;
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
    const uint32_t now = millis();
    const bool longPress = keyButton_.lastPressDurationMs() >= config_.keyLongPressMs;
    if (longPress) {
      state_.pendingKeyClick = false;
      if (state_.page == DesktopClockPage::Todo) {
        Serial.println("KEY: delete todo");
        handleTodoDelete();
        return;
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
      state_.pendingKeyClick = true;
      state_.pendingKeyClickMs = now;
    }
  }

  if (bootButton_.consumeReleased()) {
    state_.page = DesktopClockUi::nextPage(state_.page);
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

void AppController::markUiDirty() {
  state_.uiDirty = true;
}
