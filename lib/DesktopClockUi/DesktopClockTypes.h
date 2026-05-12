#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "HubService.h"
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
  int batteryEtaMinutes = -1;
  bool newMessageAlert = false;
  bool newMessageAlertInvert = false;
  HubWeather weather;
  const HubMessage* messages = nullptr;
  size_t messageCount = 0;
  size_t selectedMessage = 0;
  bool messageBodyFocused = false;
  uint16_t messageBodyScrollLine = 0;
  const HubTodo* todos = nullptr;
  size_t todoCount = 0;
  size_t selectedTodo = 0;
};
