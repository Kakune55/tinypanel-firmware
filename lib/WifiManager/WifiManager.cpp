#include "WifiManager.h"

#include <WiFi.h>
#include "esp_wifi.h"

namespace {

void enableMaxModemSleep() {
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

}  // namespace

bool WifiManager::begin(const char* ssid, const char* password, uint32_t timeoutMs) {
  singleCredential_ = {ssid, password};
  return begin(&singleCredential_, 1, timeoutMs);
}

bool WifiManager::begin(const WifiCredential* credentials, size_t credentialCount, uint32_t timeoutMs) {
  credentials_ = credentials;
  credentialCount_ = credentialCount;
  activeCredential_ = 0;

  WiFi.mode(WIFI_STA);
  enableMaxModemSleep();
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

  size_t validCount = 0;
  for (size_t i = 0; i < credentialCount_; ++i) {
    if (credentialValid(i)) {
      ++validCount;
    }
  }
  if (validCount == 0) {
    Serial.println("WiFi: no usable credentials");
    return false;
  }

  const uint32_t startMs = millis();
  constexpr size_t kMaxScanCandidates = 8;
  size_t scanCandidates[kMaxScanCandidates];
  size_t scanCandidateCount = 0;

  Serial.println("WiFi: scanning");
  const int scanCount = WiFi.scanNetworks(false, true);
  if (scanCount >= 0) {
    while (scanCandidateCount < kMaxScanCandidates) {
      int bestRssi = -1000;
      size_t bestIndex = credentialCount_;

      for (size_t credentialIndex = 0; credentialIndex < credentialCount_; ++credentialIndex) {
        if (!credentialValid(credentialIndex)) {
          continue;
        }

        bool alreadySelected = false;
        for (size_t i = 0; i < scanCandidateCount; ++i) {
          alreadySelected = alreadySelected || scanCandidates[i] == credentialIndex;
        }
        if (alreadySelected) {
          continue;
        }

        for (int networkIndex = 0; networkIndex < scanCount; ++networkIndex) {
          if (WiFi.SSID(networkIndex) == credentials_[credentialIndex].ssid && WiFi.RSSI(networkIndex) > bestRssi) {
            bestRssi = WiFi.RSSI(networkIndex);
            bestIndex = credentialIndex;
          }
        }
      }

      if (bestIndex >= credentialCount_) {
        break;
      }
      scanCandidates[scanCandidateCount++] = bestIndex;
    }
    WiFi.scanDelete();

    if (scanCandidateCount == 0) {
      Serial.println("WiFi: no configured network nearby");
      return false;
    }
  } else {
    Serial.printf("WiFi: scan failed (%d), using configured order\n", scanCount);
  }

  const size_t attemptCount = scanCandidateCount > 0 ? scanCandidateCount : validCount;
  const uint32_t perNetworkMs = max<uint32_t>(3000, timeoutMs / attemptCount);
  const size_t firstIndex = activeCredential_ < credentialCount_ ? activeCredential_ : 0;

  size_t validAttempts = 0;
  for (size_t attempt = 0; attempt < credentialCount_ && validAttempts < attemptCount; ++attempt) {
    const size_t index = scanCandidateCount > 0 ? scanCandidates[attempt] : (firstIndex + attempt) % credentialCount_;
    if (!credentialValid(index)) {
      continue;
    }
    ++validAttempts;
    if (millis() - startMs >= timeoutMs) {
      break;
    }

    const WifiCredential& credential = credentials_[index];
    Serial.printf("WiFi: connecting to %s\n", credential.ssid);
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(credential.ssid, credential.password);

    const uint32_t attemptStartMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - attemptStartMs < perNetworkMs &&
           millis() - startMs < timeoutMs) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      activeCredential_ = index;
      updateSignal();
      Serial.printf("WiFi: connected to %s, IP=%s, RSSI=%d dBm\n", WiFi.SSID().c_str(), ipAddress().c_str(), rssi());
      enableMaxModemSleep();
      return true;
    }
  }

  Serial.println("WiFi: connect failed");
  return false;
}

void WifiManager::disconnect(bool radioOff) {
  WiFi.disconnect(true, radioOff);
  if (radioOff) {
    WiFi.mode(WIFI_OFF);
  }
}

void WifiManager::updateSignal() {
  cachedRssi_ = isConnected() ? WiFi.RSSI() : 0;
}

bool WifiManager::isConfigured() const {
  for (size_t i = 0; i < credentialCount_; ++i) {
    if (credentialValid(i)) {
      return true;
    }
  }
  return false;
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
  return isConnected() ? WiFi.localIP().toString() : String();
}

int WifiManager::rssi() const {
  return isConnected() ? cachedRssi_ : 0;
}

String WifiManager::ssid() const {
  if (isConnected()) {
    return WiFi.SSID();
  }
  return credentialValid(activeCredential_) ? String(credentials_[activeCredential_].ssid) : String();
}

bool WifiManager::credentialValid(size_t index) const {
  return credentials_ != nullptr && index < credentialCount_ && credentials_[index].ssid != nullptr &&
         credentials_[index].ssid[0] != '\0';
}
