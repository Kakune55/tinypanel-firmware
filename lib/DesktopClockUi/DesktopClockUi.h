#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "HubService.h"
#include "RlcdDisplay.h"
#include "RtcClock.h"
#include "StatusBar.h"
#include "Shtc3Sensor.h"

enum class DesktopClockPage {
  Clock,
  Message,
  Dashboard,
  System,
};

struct DesktopClockUiModel {
  DesktopClockPage page = DesktopClockPage::Clock;
  BatteryStatus battery;
  Shtc3Reading environment;
  RtcDateTime now;
  bool ntpSynced = false;
  bool ntpSyncing = false;
  bool ntpSyncFailed = false;
  bool hubSyncing = false;
  bool hubSyncFailed = false;
  bool sdMounted = false;
  const char* sdStatus = "NO CARD";
  bool wifiConnected = false;
  int wifiRssi = 0;
  String wifiIp;
  String wifiSsid;
  uint32_t uptimeMs = 0;
  uint32_t freeHeap = 0;
  uint32_t freePsram = 0;
  bool newMessageAlert = false;
  bool newMessageAlertInvert = false;
  const HubMessage* messages = nullptr;
  size_t messageCount = 0;
  size_t selectedMessage = 0;
  bool messageBodyFocused = false;
  uint16_t messageBodyScrollLine = 0;
};

class DesktopClockUi {
 public:
  explicit DesktopClockUi(RlcdDisplay& display);

  void render(const DesktopClockUiModel& model);
  static DesktopClockPage nextPage(DesktopClockPage page);

 private:
  RlcdDisplay& display_;
  StatusBar statusBar_;
};
