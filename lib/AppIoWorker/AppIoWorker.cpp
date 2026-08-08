#include "AppIoWorker.h"

namespace {

const char* jobName(AppIoJobType type) {
  switch (type) {
    case AppIoJobType::WifiConnect:
      return "wifi";
    case AppIoJobType::NtpSync:
      return "ntp";
    case AppIoJobType::HubHello:
      return "hub-hello";
    case AppIoJobType::HubTelemetry:
      return "hub-telemetry";
    case AppIoJobType::HubMessages:
      return "hub-messages";
    case AppIoJobType::HubWeather:
      return "hub-weather";
    case AppIoJobType::HubTodos:
      return "hub-todos";
    case AppIoJobType::HubTodoChanges:
      return "hub-todo-changes";
    case AppIoJobType::StoreHubCredentials:
      return "store-hub-credentials";
    case AppIoJobType::StoreWeather:
      return "store-weather";
    case AppIoJobType::StoreTodos:
      return "store-todos";
    case AppIoJobType::StoreMessages:
      return "store-messages";
    case AppIoJobType::AppendBatterySamples:
      return "store-battery";
    case AppIoJobType::RefreshSd:
      return "sd-refresh";
    case AppIoJobType::None:
      return "none";
  }
  return "unknown";
}

bool persistMessages(const HubMessage* messages, size_t count, void* context) {
  AppStorage* storage = static_cast<AppStorage*>(context);
  return storage && storage->isReady() && storage->saveMessages(messages, count);
}

}  // namespace

AppIoWorker::AppIoWorker(WifiManager& wifi,
                         TimeSync& timeSync,
                         HubService& hub,
                         AppStorage& storage,
                         SdCardStorage& sdCard)
    : wifi_(wifi), timeSync_(timeSync), hub_(hub), storage_(storage), sdCard_(sdCard) {}

bool AppIoWorker::begin(uint32_t stackBytes, UBaseType_t priority, BaseType_t core) {
  if (task_) {
    return true;
  }
  if (!mutex_) {
    mutex_ = xSemaphoreCreateMutex();
  }
  if (!mutex_) {
    Serial.println("IO: mutex allocation failed");
    return false;
  }

  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "tinypanel_io", stackBytes, this, priority, &task_, core);
  if (created != pdPASS) {
    task_ = nullptr;
    Serial.println("IO: task creation failed");
    return false;
  }
  Serial.printf("IO: worker started on core %d\n", static_cast<int>(core));
  return true;
}

bool AppIoWorker::submit(const AppIoRequest& request) {
  if (request.type == AppIoJobType::None) {
    return false;
  }
  if (!beginSubmit(request.type)) {
    return false;
  }
  request_ = request;
  request_.telemetry.deviceId = nullptr;
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::beginSubmit(AppIoJobType type) {
  if (!task_ || !mutex_ || type == AppIoJobType::None) {
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (busy_ || resultPending_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  request_ = {};
  request_.type = type;
  busy_ = true;
  return true;
}

bool AppIoWorker::submitHubCredentials(const StoredHubCredentials& credentials) {
  if (!beginSubmit(AppIoJobType::StoreHubCredentials)) return false;
  storedHubCredentials_ = credentials;
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::submitWeather(const HubWeather& weather) {
  if (!beginSubmit(AppIoJobType::StoreWeather)) return false;
  storedWeather_ = weather;
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::submitTodos(const HubTodo* todos,
                              size_t count,
                              const HubTodoDelete* deletes,
                              size_t deleteCount) {
  if ((!todos && count > 0) || (!deletes && deleteCount > 0) ||
      !beginSubmit(AppIoJobType::StoreTodos)) return false;
  storedTodoCount_ = min(count, HubService::MaxTodos);
  for (size_t i = 0; i < storedTodoCount_; ++i) storedTodos_[i] = todos[i];
  storedTodoDeleteCount_ = min(deleteCount, HubService::MaxTodos);
  for (size_t i = 0; i < storedTodoDeleteCount_; ++i) storedTodoDeletes_[i] = deletes[i];
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::submitMessages(const HubMessage* messages, size_t count) {
  if ((!messages && count > 0) || !beginSubmit(AppIoJobType::StoreMessages)) return false;
  storedMessageCount_ = min(count, HubService::MaxMessages);
  for (size_t i = 0; i < storedMessageCount_; ++i) storedMessages_[i] = messages[i];
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::submitBatterySamples(const AppBatteryLogSample* samples, size_t count) {
  if (!samples || count == 0 || !beginSubmit(AppIoJobType::AppendBatterySamples)) return false;
  storedBatterySampleCount_ = min(count, static_cast<size_t>(3));
  for (size_t i = 0; i < storedBatterySampleCount_; ++i) storedBatterySamples_[i] = samples[i];
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::submitSdRefresh(bool mountIfNeeded) {
  if (!beginSubmit(AppIoJobType::RefreshSd)) return false;
  mountSdIfNeeded_ = mountIfNeeded;
  xSemaphoreGive(mutex_);
  xTaskNotifyGive(task_);
  return true;
}

bool AppIoWorker::takeResult(AppIoResult& result) {
  if (!mutex_) {
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!resultPending_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  result = result_;
  resultPending_ = false;
  busy_ = false;
  xSemaphoreGive(mutex_);
  return true;
}

bool AppIoWorker::isBusy() const {
  if (!mutex_) {
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool busy = busy_ || resultPending_;
  xSemaphoreGive(mutex_);
  return busy;
}

bool AppIoWorker::isReady() const {
  return task_ != nullptr;
}

void AppIoWorker::taskEntry(void* context) {
  static_cast<AppIoWorker*>(context)->taskLoop();
}

void AppIoWorker::taskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    xSemaphoreTake(mutex_, portMAX_DELAY);
    AppIoRequest& request = request_;
    request.telemetry.deviceId = request.telemetryDeviceId.c_str();
    xSemaphoreGive(mutex_);

    Serial.printf("IO: %s start\n", jobName(request.type));
    AppIoResult result = execute(request);
    Serial.printf("IO: %s end ok=%d status=%d retry=%d duration=%lu ms heap=%u stack=%u\n",
                  jobName(request.type),
                  result.operationOk ? 1 : 0,
                  result.request.statusCode,
                  result.request.retryable ? 1 : 0,
                  static_cast<unsigned long>(result.durationMs),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    xSemaphoreTake(mutex_, portMAX_DELAY);
    result_ = result;
    resultPending_ = true;
    xSemaphoreGive(mutex_);
  }
}

AppIoResult AppIoWorker::execute(AppIoRequest& request) {
  AppIoResult result;
  result.type = request.type;
  const uint32_t startedMs = millis();

  switch (request.type) {
    case AppIoJobType::WifiConnect:
      result.request.attempted = true;
      result.operationOk = wifi_.connect(request.timeoutMs);
      result.request.ok = result.operationOk;
      break;
    case AppIoJobType::NtpSync:
      result.request.attempted = true;
      result.operationOk = timeSync_.begin(request.timezone.c_str()) &&
                           timeSync_.waitForTime(result.networkTime, request.timeoutMs);
      result.request.ok = result.operationOk;
      break;
    case AppIoJobType::HubHello:
      result.hello = hub_.hello(wifi_.isConnected(), nullptr);
      result.request = result.hello;
      result.operationOk = result.hello.ok;
      break;
    case AppIoJobType::HubTelemetry:
      result.request = hub_.syncTelemetry(request.telemetry, request.force, wifi_.isConnected(), nullptr);
      result.operationOk = !result.request.attempted || result.request.ok;
      break;
    case AppIoJobType::HubMessages:
      result.request = hub_.pollMessages(
          request.force, wifi_.isConnected(), nullptr, millis(), persistMessages, &storage_);
      result.operationOk = !result.request.attempted || result.request.ok;
      break;
    case AppIoJobType::HubWeather:
      result.request = hub_.pollWeather(request.force, wifi_.isConnected(), nullptr);
      result.operationOk = !result.request.attempted || result.request.ok;
      break;
    case AppIoJobType::HubTodos:
      result.request = hub_.pollTodos(request.force, wifi_.isConnected(), nullptr);
      result.operationOk = !result.request.attempted || result.request.ok;
      break;
    case AppIoJobType::HubTodoChanges:
      result.request = hub_.syncTodoChanges(wifi_.isConnected(), nullptr);
      result.operationOk = !result.request.attempted || result.request.ok;
      break;
    case AppIoJobType::StoreHubCredentials:
      result.operationOk = storage_.isReady() && storage_.saveHubCredentials(storedHubCredentials_);
      break;
    case AppIoJobType::StoreWeather:
      result.operationOk = storage_.isReady() && storage_.saveWeather(storedWeather_);
      break;
    case AppIoJobType::StoreTodos:
      result.operationOk = storage_.isReady() &&
                           storage_.saveTodos(storedTodos_, storedTodoCount_,
                                              storedTodoDeletes_, storedTodoDeleteCount_);
      break;
    case AppIoJobType::StoreMessages:
      result.operationOk = storage_.isReady() && storage_.saveMessages(storedMessages_, storedMessageCount_);
      break;
    case AppIoJobType::AppendBatterySamples:
      result.operationOk = storage_.isReady();
      for (size_t i = 0; i < storedBatterySampleCount_ && result.operationOk; ++i) {
        result.operationOk = storage_.appendBatterySample(storedBatterySamples_[i].battery,
                                                         storedBatterySamples_[i].time,
                                                         storedBatterySamples_[i].uptimeS);
      }
      break;
    case AppIoJobType::RefreshSd:
      result.sdMounted = sdCard_.isMounted();
      if (!result.sdMounted && mountSdIfNeeded_) {
        result.sdMounted = sdCard_.begin();
        if (result.sdMounted) storage_.begin(sdCard_);
      } else if (result.sdMounted) {
        result.sdMounted = sdCard_.verifyMounted();
      }
      result.storageReady = storage_.isReady();
      result.sdTotalMb = result.sdMounted ? sdCard_.cardSizeBytes() / (1024UL * 1024UL) : 0;
      result.sdUsedMb = result.sdMounted ? sdCard_.usedBytes() / (1024UL * 1024UL) : 0;
      result.operationOk = result.sdMounted;
      break;
    case AppIoJobType::None:
      break;
  }

  result.durationMs = millis() - startedMs;
  return result;
}
