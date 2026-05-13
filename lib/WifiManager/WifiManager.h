#pragma once

#include <Arduino.h>

#include "WifiCredential.h"

class WifiManager {
public:
  bool begin(const char* ssid, const char* password, uint32_t timeoutMs = 15000);
  bool begin(const WifiCredential* credentials, size_t credentialCount, uint32_t timeoutMs = 15000);
  bool connect(uint32_t timeoutMs = 15000);
  void disconnect(bool radioOff = false);
  void updateSignal();

  bool isConfigured() const;
  bool isConnected() const;
  String ipAddress() const;
  int rssi() const;
  String ssid() const;

private:
  bool credentialValid(size_t index) const;

  WifiCredential singleCredential_ = {nullptr, nullptr};
  const WifiCredential* credentials_ = nullptr;
  size_t credentialCount_ = 0;
  size_t activeCredential_ = 0;
  int cachedRssi_ = 0;
};
