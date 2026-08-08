#pragma once

#include <Arduino.h>

#include "HubService.h"
#include "RtcClock.h"
#include "TimeSync.h"
#include "WifiManager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum class AppIoJobType : uint8_t {
  None,
  WifiConnect,
  NtpSync,
  HubHello,
  HubTelemetry,
  HubMessages,
  HubWeather,
  HubTodos,
  HubTodoChanges,
};

struct AppIoRequest {
  AppIoJobType type = AppIoJobType::None;
  bool force = false;
  uint32_t timeoutMs = 0;
  String timezone;
  String telemetryDeviceId;
  HubTelemetrySnapshot telemetry;
};

struct AppIoResult {
  AppIoJobType type = AppIoJobType::None;
  HubRequestResult request;
  HubHelloResult hello;
  RtcDateTime networkTime;
  bool operationOk = false;
  uint32_t durationMs = 0;
};

class AppIoWorker {
 public:
  AppIoWorker(WifiManager& wifi, TimeSync& timeSync, HubService& hub);

  bool begin(uint32_t stackBytes = 12288, UBaseType_t priority = 1, BaseType_t core = 0);
  bool submit(const AppIoRequest& request);
  bool takeResult(AppIoResult& result);
  bool isBusy() const;
  bool isReady() const;

 private:
  static void taskEntry(void* context);
  void taskLoop();
  AppIoResult execute(AppIoRequest& request);

  WifiManager& wifi_;
  TimeSync& timeSync_;
  HubService& hub_;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  AppIoRequest request_;
  AppIoResult result_;
  bool busy_ = false;
  bool resultPending_ = false;
};
