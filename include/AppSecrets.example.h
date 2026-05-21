#pragma once

namespace AppSecrets {

constexpr const char* WifiSsid = "YOUR_WIFI_SSID";
constexpr const char* WifiPassword = "YOUR_WIFI_PASSWORD";

constexpr const char* HubServerBaseURL = "http://192.168.1.2:8080/api/v1";
// New Hub uses this as the device secret. Leave as YOUR_* to let the device
// obtain and persist a secret with POST /api/v1/device/hello.
constexpr const char* HubServerApiKey = "YOUR_HUB_DEVICE_SECRET";

}  // namespace AppSecrets
