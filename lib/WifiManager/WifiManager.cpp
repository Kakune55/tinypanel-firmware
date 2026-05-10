#include "WifiManager.h"

#include <WiFi.h>

bool WifiManager::begin(const char* ssid, const char* password, uint32_t timeoutMs) {
  ssid_ = ssid;
  password_ = password;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  return connect(timeoutMs);
}

bool WifiManager::connect(uint32_t timeoutMs) {
  if (!isConfigured()) {
    Serial.println("WiFi: not configured");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("WiFi: connecting to %s\n", ssid_);
  WiFi.begin(ssid_, password_);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: connect failed");
    return false;
  }

  Serial.printf("WiFi: connected, IP=%s, RSSI=%d dBm\n", ipAddress().c_str(), rssi());
  return true;
}

void WifiManager::disconnect(bool radioOff) {
  WiFi.disconnect(true, radioOff);
  if (radioOff) {
    WiFi.mode(WIFI_OFF);
  }
}

bool WifiManager::isConfigured() const {
  return ssid_ != nullptr && ssid_[0] != '\0';
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
  return isConnected() ? WiFi.localIP().toString() : String();
}

int WifiManager::rssi() const {
  return isConnected() ? WiFi.RSSI() : 0;
}

String WifiManager::ssid() const {
  return isConnected() ? WiFi.SSID() : String(ssid_ == nullptr ? "" : ssid_);
}
