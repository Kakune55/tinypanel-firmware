#pragma once

#include <Arduino.h>

class WifiManager {
public:
  bool begin(const char* ssid, const char* password, uint32_t timeoutMs = 15000);
  bool connect(uint32_t timeoutMs = 15000);
  void disconnect(bool radioOff = false);
  void updateSignal();

  bool isConfigured() const;
  bool isConnected() const;
  String ipAddress() const;
  int rssi() const;
  String ssid() const;

private:
  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  int cachedRssi_ = 0;
};
