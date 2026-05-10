#include "AppController.h"

namespace {

AppController* activeController = nullptr;

}  // namespace

AppController::AppController(const AppControllerConfig& config,
                             BatteryMonitor& battery,
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

void AppController::setSdMounted(bool mounted) {
  state_.sdMounted = mounted;
  markUiDirty();
}

bool AppController::sdMounted() const {
  return state_.sdMounted;
}

bool AppController::ntpSynced() const {
  return state_.ntpSynced;
}

void AppController::readSensors(bool force) {
  const uint32_t now = millis();

  if (force || now - state_.lastBatteryMs >= config_.batteryPollMs) {
    state_.battery = battery_.readStatus();
    state_.lastBatteryMs = now;
    markUiDirty();
  }

  if (force || now - state_.lastEnvironmentMs >= config_.environmentPollMs) {
    shtc3_.read(state_.environment);
    state_.lastEnvironmentMs = now;
    markUiDirty();
  }

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
    if (state_.page != DesktopClockPage::Message) {
      state_.newMessageAlert = true;
    }
  }
  markUiDirty();
}

void AppController::loopOnce() {
  handleButtons();
  handlePendingKeyClick();
  handleWifi();
  trySyncTime(false);
  readSensors(false);
  syncHubTelemetry(false);
  pollHubMessages(false);

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
  model.wifiConnected = wifi_.isConnected();
  model.wifiRssi = wifi_.rssi();
  model.wifiIp = wifi_.isConnected() ? wifi_.ipAddress() : "";
  model.wifiSsid = wifi_.ssid();
  model.uptimeMs = millis();
  model.freeHeap = ESP.getFreeHeap();
  model.freePsram = ESP.getFreePsram();
  model.newMessageAlert = state_.newMessageAlert;
  model.newMessageAlertInvert =
      state_.newMessageAlert && ((millis() / config_.newMessageBlinkMs) % 2 == 1);
  model.messages = hub_.messages();
  model.messageCount = hub_.messageCount();
  model.selectedMessage = state_.selectedMessage;
  model.messageBodyFocused = state_.messageBodyFocused;
  model.messageBodyScrollLine = state_.messageBodyScrollLine;
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

HubTelemetrySnapshot AppController::buildHubTelemetrySnapshot() const {
  HubTelemetrySnapshot snapshot;
  snapshot.deviceId = config_.deviceId;
  snapshot.bootId = state_.bootId;
  snapshot.reportTimestamp = formatRtcTimestamp(state_.now);
  snapshot.uptimeS = millis() / 1000UL;

  snapshot.battery = state_.battery;
  snapshot.usbConnected = false;

  snapshot.environment = state_.environment;

  snapshot.wifiConnected = wifi_.isConnected();
  snapshot.wifiSsid = wifi_.ssid();
  snapshot.wifiRssiDbm = wifi_.rssi();
  snapshot.wifiIp = wifi_.ipAddress();

  snapshot.freeHeapBytes = ESP.getFreeHeap();
  snapshot.freePsramBytes = ESP.getFreePsram();
  snapshot.ntpSync = state_.ntpSynced;

  snapshot.sdCardPresent = sdCard_.isMounted();
  snapshot.sdCardTotalMb = sdCard_.cardSizeBytes() / (1024UL * 1024UL);
  snapshot.sdCardUsedMb = sdCard_.usedBytes() / (1024UL * 1024UL);
  return snapshot;
}

uint16_t AppController::messageBodyLineCount(const String& text) const {
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
  constexpr uint16_t kPageLines = 9;
  if (lineCount <= kPageLines || state_.messageBodyScrollLine + kPageLines >= lineCount) {
    state_.messageBodyScrollLine = 0;
  } else {
    state_.messageBodyScrollLine += kPageLines;
  }
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

  Serial.println("KEY: manual refresh");
  if (!sdCard_.isMounted()) {
    setSdMounted(sdCard_.begin());
    sdCard_.printInfo(Serial);
  }
  readSensors(true);
  pollHubMessages(true);
  markUiDirty();
}

void AppController::handleKeyDoubleClick() {
  if (state_.page != DesktopClockPage::Message) {
    handleSingleKeyClick();
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
      Serial.println("KEY: force network sync");
      if (!sdCard_.isMounted()) {
        setSdMounted(sdCard_.begin());
        sdCard_.printInfo(Serial);
      }
      readSensors(true);
    }
    if (longPress && !wifi_.isConnected() && config_.wifiConfigured) {
      wifi_.connect(8000);
    }
    if (longPress) {
      trySyncTime(true);
      syncHubTelemetry(true);
      pollHubMessages(true);
      markUiDirty();
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
    state_.pendingKeyClick = false;
    markUiDirty();
    Serial.println("BOOT: page switched");
  }
}

void AppController::markUiDirty() {
  state_.uiDirty = true;
}
