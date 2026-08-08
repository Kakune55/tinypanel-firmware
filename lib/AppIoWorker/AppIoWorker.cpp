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

AppIoWorker::AppIoWorker(WifiManager& wifi, TimeSync& timeSync, HubService& hub, AppStorage& storage)
    : wifi_(wifi), timeSync_(timeSync), hub_(hub), storage_(storage) {}

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
  if (!task_ || !mutex_ || request.type == AppIoJobType::None) {
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (busy_ || resultPending_) {
    xSemaphoreGive(mutex_);
    return false;
  }
  request_ = request;
  request_.telemetry.deviceId = nullptr;
  busy_ = true;
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

    AppIoRequest request;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    request = request_;
    xSemaphoreGive(mutex_);
    request.telemetry.deviceId = request.telemetryDeviceId.c_str();

    Serial.printf("IO: %s start\n", jobName(request.type));
    AppIoResult result = execute(request);
    Serial.printf("IO: %s end ok=%d duration=%lu ms\n",
                  jobName(request.type),
                  result.operationOk ? 1 : 0,
                  static_cast<unsigned long>(result.durationMs));

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
      result.operationOk = result.request.ok;
      break;
    case AppIoJobType::HubMessages:
      result.request = hub_.pollMessages(
          request.force, wifi_.isConnected(), nullptr, millis(), persistMessages, &storage_);
      result.operationOk = result.request.ok;
      break;
    case AppIoJobType::HubWeather:
      result.request = hub_.pollWeather(request.force, wifi_.isConnected(), nullptr);
      result.operationOk = result.request.ok;
      break;
    case AppIoJobType::HubTodos:
      result.request = hub_.pollTodos(request.force, wifi_.isConnected(), nullptr);
      result.operationOk = result.request.ok;
      break;
    case AppIoJobType::HubTodoChanges:
      result.request = hub_.syncTodoChanges(wifi_.isConnected(), nullptr);
      result.operationOk = !result.request.attempted || result.request.ok;
      break;
    case AppIoJobType::None:
      break;
  }

  result.durationMs = millis() - startedMs;
  return result;
}
