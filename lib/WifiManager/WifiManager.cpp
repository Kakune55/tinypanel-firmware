#include "WifiManager.h"

#include <WiFi.h>
#include "esp_wifi.h"

namespace {

const char* authModeName(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN:
      return "open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI";
    default:
      return "unknown";
  }
}

const char* signalLevel(int32_t rssi) {
  if (rssi >= -50) {
    return "excellent";
  }
  if (rssi >= -60) {
    return "good";
  }
  if (rssi >= -70) {
    return "fair";
  }
  return "weak";
}

const char* connectionStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "SSID unavailable";
    case WL_SCAN_COMPLETED:
      return "scan completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "authentication/connect failed";
    case WL_CONNECTION_LOST:
      return "connection lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "unknown";
  }
}

void enableMaxModemSleep() {
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

}  // namespace

bool WifiManager::configure(const char* ssid, const char* password) {
  singleCredential_ = {ssid, password};
  return configure(&singleCredential_, 1);
}

bool WifiManager::configure(const WifiCredential* credentials, size_t credentialCount) {
  credentials_ = credentials;
  credentialCount_ = credentialCount;
  activeCredential_ = 0;
  hasActiveCredential_ = false;
  return isConfigured();
}

bool WifiManager::begin(const char* ssid, const char* password, uint32_t timeoutMs) {
  configure(ssid, password);
  return connect(timeoutMs);
}

bool WifiManager::begin(const WifiCredential* credentials, size_t credentialCount, uint32_t timeoutMs) {
  configure(credentials, credentialCount);
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

  WiFi.mode(WIFI_STA);
  enableMaxModemSleep();

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
  if (hasActiveCredential_ && credentialValid(activeCredential_)) {
    const uint32_t quickTimeoutMs = min<uint32_t>(timeoutMs, 5000);
    Serial.println("WiFi: reconnecting to last network");
    if (connectCredential(activeCredential_, quickTimeoutMs)) {
      return true;
    }
    if (millis() - startMs >= timeoutMs) {
      Serial.println("WiFi: connect failed");
      return false;
    }
  }

  constexpr size_t kMaxScanCandidates = 8;
  size_t scanCandidates[kMaxScanCandidates];
  size_t scanCandidateCount = 0;

  Serial.println("WiFi: scanning");
  const int scanCount = WiFi.scanNetworks(false, true);
  if (scanCount >= 0) {
    Serial.printf("WiFi: found %d network(s)\n", scanCount);
    for (int networkIndex = 0; networkIndex < scanCount; ++networkIndex) {
      const String scannedSsid = WiFi.SSID(networkIndex);
      bool configured = false;
      for (size_t credentialIndex = 0; credentialIndex < credentialCount_; ++credentialIndex) {
        configured = configured ||
                     (credentialValid(credentialIndex) && scannedSsid == credentials_[credentialIndex].ssid);
      }

      const int32_t scannedRssi = WiFi.RSSI(networkIndex);
      Serial.printf("WiFi:   %d. SSID=\"%s\", RSSI=%ld dBm (%s), channel=%ld, security=%s%s\n",
                    networkIndex + 1,
                    scannedSsid.isEmpty() ? "<hidden>" : scannedSsid.c_str(),
                    static_cast<long>(scannedRssi),
                    signalLevel(scannedRssi),
                    static_cast<long>(WiFi.channel(networkIndex)),
                    authModeName(WiFi.encryptionType(networkIndex)),
                    configured ? ", configured" : "");
    }

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

    const uint32_t remainingMs = timeoutMs - (millis() - startMs);
    if (connectCredential(index, min(perNetworkMs, remainingMs))) {
      return true;
    }
  }

  Serial.println("WiFi: connect failed");
  return false;
}

void WifiManager::disconnect(bool radioOff) {
  WiFi.disconnect(radioOff, false);
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
  return hasActiveCredential_ && credentialValid(activeCredential_) ? String(credentials_[activeCredential_].ssid) : String();
}

bool WifiManager::credentialValid(size_t index) const {
  return credentials_ != nullptr && index < credentialCount_ && credentials_[index].ssid != nullptr &&
         credentials_[index].ssid[0] != '\0';
}

bool WifiManager::connectCredential(size_t index, uint32_t timeoutMs) {
  if (!credentialValid(index) || timeoutMs == 0) {
    return false;
  }

  const WifiCredential& credential = credentials_[index];
  Serial.printf("WiFi: connecting to %s\n", credential.ssid);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(credential.ssid, credential.password);

  const uint32_t attemptStartMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - attemptStartMs < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    const wl_status_t status = WiFi.status();
    Serial.printf("WiFi: connection to %s failed, status=%s (%d)\n",
                  credential.ssid,
                  connectionStatusName(status),
                  static_cast<int>(status));
    return false;
  }

  activeCredential_ = index;
  hasActiveCredential_ = true;
  updateSignal();
  Serial.printf("WiFi: connected to %s, IP=%s, RSSI=%d dBm\n", WiFi.SSID().c_str(), ipAddress().c_str(), rssi());
  enableMaxModemSleep();
  return true;
}
