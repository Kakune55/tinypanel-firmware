#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "HubTypes.h"
#include "RtcClock.h"
#include "Shtc3Sensor.h"

enum class DesktopClockPage {
  Clock,
  Message,
  Todo,
  Weather,
  System,
};

constexpr int kDesktopClockPageCount = 5;

struct BatteryChartPoint {
  uint16_t minute = 0;
  uint8_t percent = 0;
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
  bool storageReady = false;
  uint32_t sdCardTotalMb = 0;
  uint32_t sdCardUsedMb = 0;
  bool wifiConfigured = false;
  bool wifiConfigFromSd = false;
  bool batteryCurveFromSd = false;
  bool messagesRestoredFromSd = false;
  uint32_t batteryLogIntervalMs = 0;
  uint8_t selectedSystemMenuItem = 0;
  uint8_t selectedSystemAction = 0;
  bool systemActionFocused = false;
  bool wifiConnected = false;
  bool wifiDisabled = false;
  bool wifiAutoDisabled = false;
  uint8_t wifiFailureCount = 0;
  uint8_t wifiMaxFailures = 0;
  int wifiRssi = 0;
  String wifiIp;
  String wifiSsid;
  uint32_t uptimeMs = 0;
  uint32_t freeHeap = 0;
  uint32_t heapSize = 0;
  uint32_t freePsram = 0;
  uint32_t psramSize = 0;
  uint32_t cpuMhz = 0;
  int batteryEtaMinutes = -1;
  const BatteryChartPoint* batteryChart = nullptr;
  size_t batteryChartCount = 0;
  uint16_t batteryChartNowMinute = 0;
  uint16_t batteryChartPredictedZeroMinute = 0;
  bool newMessageAlert = false;
  bool newMessageAlertInvert = false;
  HubWeather weather;
  const HubMessage* messages = nullptr;
  size_t messageCount = 0;
  size_t selectedMessage = 0;
  bool messageBodyFocused = false;
  uint16_t messageBodyScrollLine = 0;
  uint8_t messageDeleteProgress = 0;
  const HubTodo* todos = nullptr;
  size_t todoCount = 0;
  size_t selectedTodo = 0;
};
