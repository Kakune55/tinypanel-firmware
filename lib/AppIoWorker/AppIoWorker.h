#pragma once

#include <Arduino.h>

#include "HubService.h"
#include "AppStorage.h"
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
  StoreHubCredentials,
  StoreWeather,
  StoreTodos,
  StoreMessages,
  AppendBatterySamples,
  RefreshSd,
};

struct AppBatteryLogSample {
  BatteryStatus battery;
  RtcDateTime time;
  uint32_t uptimeS = 0;
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
  bool sdMounted = false;
  bool storageReady = false;
  uint32_t sdTotalMb = 0;
  uint32_t sdUsedMb = 0;
};

class AppIoWorker {
 public:
  AppIoWorker(WifiManager& wifi,
              TimeSync& timeSync,
              HubService& hub,
              AppStorage& storage,
              SdCardStorage& sdCard);

  bool begin(uint32_t stackBytes = 12288, UBaseType_t priority = 1, BaseType_t core = 0);
  bool submit(const AppIoRequest& request);
  bool submitHubCredentials(const StoredHubCredentials& credentials);
  bool submitWeather(const HubWeather& weather);
  bool submitTodos(const HubTodo* todos,
                   size_t count,
                   const HubTodoDelete* deletes,
                   size_t deleteCount);
  bool submitMessages(const HubMessage* messages, size_t count);
  bool submitBatterySamples(const AppBatteryLogSample* samples, size_t count);
  bool submitSdRefresh(bool mountIfNeeded);
  bool takeResult(AppIoResult& result);
  bool isBusy() const;
  bool isReady() const;

 private:
  static void taskEntry(void* context);
  void taskLoop();
  AppIoResult execute(AppIoRequest& request);
  bool beginSubmit(AppIoJobType type);

  WifiManager& wifi_;
  TimeSync& timeSync_;
  HubService& hub_;
  AppStorage& storage_;
  SdCardStorage& sdCard_;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  AppIoRequest request_;
  AppIoResult result_;
  bool busy_ = false;
  bool resultPending_ = false;
  StoredHubCredentials storedHubCredentials_;
  HubWeather storedWeather_;
  HubTodo storedTodos_[HubService::MaxTodos];
  size_t storedTodoCount_ = 0;
  HubTodoDelete storedTodoDeletes_[HubService::MaxTodos];
  size_t storedTodoDeleteCount_ = 0;
  HubMessage storedMessages_[HubService::MaxMessages];
  size_t storedMessageCount_ = 0;
  AppBatteryLogSample storedBatterySamples_[3];
  size_t storedBatterySampleCount_ = 0;
  bool mountSdIfNeeded_ = false;
};
