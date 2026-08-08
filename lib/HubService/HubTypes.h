#pragma once

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "Shtc3Sensor.h"

constexpr size_t kHubMaxMessages = 10;
constexpr size_t kHubMaxTodos = 12;

struct HubTelemetrySnapshot {
  const char* deviceId = "tinypanel-001";
  String bootId;
  String reportTimestamp;
  uint32_t uptimeS = 0;

  BatteryStatus battery;
  bool usbConnected = false;

  Shtc3Reading environment;

  bool wifiConnected = false;
  String wifiSsid;
  int wifiRssiDbm = 0;
  String wifiIp;

  uint32_t freeHeapBytes = 0;
  uint32_t freePsramBytes = 0;
  bool ntpSync = false;

  bool sdCardPresent = false;
  uint32_t sdCardTotalMb = 0;
  uint32_t sdCardUsedMb = 0;
};

struct HubRequestResult {
  bool attempted = false;
  bool ok = false;
  int statusCode = 0;
  bool changed = false;
  bool persisted = false;
  bool retryable = false;
};

struct HubMessage {
  int id = 0;
  String channel;
  String author;
  String body;
  String createdAt;
};

struct HubTodo {
  int id = 0;
  String text;
  int status = 0;
  int version = 0;
  String createdAt;
  String updatedAt;
  bool dirty = false;
};

struct HubTodoDelete {
  int id = 0;
  int version = 0;
};

struct HubWeatherHourly {
  String time;
  String condition;
  String icon;
  int temperature = 0;
  int humidity = 0;
  float precipitation = 0.0f;
  int precipProbability = -1;
  String windDirection;
  String windScale;
  int windSpeed = 0;
};

struct HubWeatherDaily {
  String date;
  String sunrise;
  String sunset;
  String conditionDay;
  String conditionNight;
  String iconDay;
  String iconNight;
  int temperatureMin = 0;
  int temperatureMax = 0;
  int humidity = 0;
  float precipitation = 0.0f;
  int precipProbability = -1;
  String windDirectionDay;
  String windScaleDay;
  int windSpeedDay = 0;
  String windDirectionNight;
  String windScaleNight;
  int windSpeedNight = 0;
};

struct HubWeather {
  bool valid = false;
  String location;
  String condition;
  String icon;
  int temperature = 0;
  int humidity = 0;
  String updatedAt;
  static constexpr size_t MaxHourly = 16;
  static constexpr size_t MaxDaily = 4;
  HubWeatherHourly hourly[MaxHourly];
  HubWeatherDaily daily[MaxDaily];
  size_t hourlyCount = 0;
  size_t dailyCount = 0;
};
