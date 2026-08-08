#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "HubTypes.h"

struct HubHelloResult : HubRequestResult {
  String deviceSecret;
  String bindCode;
  uint32_t bindCodeTtlS = 0;
  bool bound = false;
  String name;
  String serverTime;
};

using HubStateChangedCallback = void (*)();

enum class HubSyncState {
  Idle,
  Syncing,
  Failed,
};

struct HubStateSnapshot {
  bool configured = false;
  bool bound = false;
  bool syncing = false;
  bool failed = false;
  String deviceId;
  String bindCode;
  String deviceName;
  HubMessage messages[kHubMaxMessages];
  size_t messageCount = 0;
  HubWeather weather;
  HubTodo todos[kHubMaxTodos];
  size_t todoCount = 0;
};

class HubService {
public:
  static constexpr size_t MaxMessages = kHubMaxMessages;
  static constexpr size_t MaxTodos = kHubMaxTodos;

  HubService();
  void begin(const char* baseUrl, const char* deviceSecret, const char* deviceId);
  void setDeviceSecret(const char* deviceSecret);
  void setDeviceBinding(bool bound, const char* bindCode, const char* name);
  void configureTelemetry(uint32_t intervalMs, uint32_t syncIconMinMs);
  void configureMessages(const char* channel, uint32_t pollIntervalMs, uint8_t limit);
  void configureWeather(uint32_t pollIntervalMs);
  void configureTodos(uint32_t pollIntervalMs, uint8_t limit = MaxTodos);
  void setVerbose(bool verbose);
  bool isConfigured() const;
  bool canHello() const;
  const String& deviceId() const;
  bool isBound() const;
  const String& bindCode() const;
  const String& deviceName() const;
  bool isSyncing() const;
  bool hasFailed() const;
  bool update(uint32_t nowMs = millis());
  HubHelloResult hello(bool networkReady,
                       HubStateChangedCallback onStateChanged = nullptr,
                       uint32_t nowMs = millis());
  HubRequestResult syncTelemetry(const HubTelemetrySnapshot& snapshot,
                                 bool force,
                                 bool networkReady,
                                 HubStateChangedCallback onStateChanged = nullptr,
                                 uint32_t nowMs = millis());
  HubRequestResult pollMessages(bool force,
                                bool networkReady,
                                HubStateChangedCallback onStateChanged = nullptr,
                                uint32_t nowMs = millis());
  HubRequestResult pollWeather(bool force,
                               bool networkReady,
                               HubStateChangedCallback onStateChanged = nullptr,
                               uint32_t nowMs = millis());
  HubRequestResult pollTodos(bool force,
                             bool networkReady,
                             HubStateChangedCallback onStateChanged = nullptr,
                             uint32_t nowMs = millis());
  HubRequestResult syncTodoChanges(bool networkReady,
                                   HubStateChangedCallback onStateChanged = nullptr,
                                   uint32_t nowMs = millis());
  bool setTodoStatusLocal(size_t index, int status);
  bool deleteTodoLocal(size_t index);
  size_t messageCount() const;
  const HubMessage* messages() const;
  const HubMessage* messageAt(size_t index) const;
  bool setMessages(const HubMessage* messages, size_t count);
  bool deleteMessageLocal(size_t index);
  void clearMessagesLocal();
  const HubWeather& weather() const;
  bool setWeather(const HubWeather& weather);
  size_t todoCount() const;
  const HubTodo* todos() const;
  const HubTodo* todoAt(size_t index) const;
  bool setTodos(const HubTodo* todos, size_t count);
  void snapshot(HubStateSnapshot& out) const;

private:
  HubRequestResult sendTelemetry(const HubTelemetrySnapshot& snapshot);
  HubHelloResult sendHello();
  HubRequestResult syncSubscription();
  HubRequestResult fetchWeather();
  HubRequestResult fetchTodos();
  bool parseWeather(JsonVariantConst source, HubWeather& weather) const;
  bool parseMessage(JsonObjectConst item, HubMessage& out) const;
  HubRequestResult ackMessages(const int* ids, size_t count);
  void storeMessage(const HubMessage& message);
  bool hasMessage(int id) const;

  enum class AuthMode {
    None,
    Device,
  };

  HubRequestResult postJson(AuthMode auth, const char* path, const char* body, size_t bodyLen, JsonDocument* response, const char* label);
  HubRequestResult getJson(AuthMode auth, const char* path, JsonDocument& doc, const char* label);
  HubRequestResult getJsonFiltered(AuthMode auth,
                                   const char* path,
                                   JsonDocument& doc,
                                   const char* label,
                                   JsonDocument& filter);
  HubRequestResult requestJson(const char* method,
                               AuthMode auth,
                               const char* path,
                               const char* body,
                               size_t bodyLen,
                               JsonDocument* response,
                               const char* label,
                               JsonDocument* filter = nullptr);
  bool telemetryDue(bool force, uint32_t nowMs) const;
  bool messagePollDue(bool force, uint32_t nowMs) const;
  bool weatherPollDue(bool force, uint32_t nowMs) const;
  bool todoPollDue(bool force, uint32_t nowMs) const;
  void beginRequest(uint32_t nowMs, HubStateChangedCallback onStateChanged);
  void completeRequest(const HubRequestResult& result, uint32_t nowMs);
  bool urlEncode(const char* value, char* out, size_t outSize) const;
  bool timeReached(uint32_t nowMs, uint32_t targetMs) const;
  bool hasUsableCredential(const char* value) const;
  void lockState() const;
  void unlockState() const;

  String baseUrl_;
  String deviceSecret_;
  String deviceId_;
  String bindCode_;
  String deviceName_;
  bool bound_ = false;
  uint32_t sequence_ = 0;
  uint32_t telemetryIntervalMs_ = 5UL * 60UL * 1000UL;
  uint32_t messagePollIntervalMs_ = 60UL * 1000UL;
  uint32_t weatherPollIntervalMs_ = 10UL * 60UL * 1000UL;
  uint32_t todoPollIntervalMs_ = 60UL * 1000UL;
  uint32_t syncIconMinMs_ = 3000;
  uint32_t lastTelemetryMs_ = 0;
  uint32_t lastMessagePollMs_ = 0;
  uint32_t lastWeatherPollMs_ = 0;
  uint32_t lastTodoPollMs_ = 0;
  uint32_t syncMinUntilMs_ = 0;
  HubSyncState syncState_ = HubSyncState::Idle;
  bool requestResultPending_ = false;
  bool lastRequestOk_ = true;
  bool verbose_ = false;
  String messageChannel_ = "desk";
  uint8_t messageLimit_ = MaxMessages;
  HubMessage messages_[MaxMessages];
  size_t messageCount_ = 0;
  HubWeather weather_;
  uint8_t todoLimit_ = MaxTodos;
  HubTodo todos_[MaxTodos];
  size_t todoCount_ = 0;
  WiFiClient client_;
  WiFiClientSecure secureClient_;
  JsonDocument jsonDoc_;
  JsonDocument responseDoc_;
  mutable SemaphoreHandle_t stateMutex_ = nullptr;
};
