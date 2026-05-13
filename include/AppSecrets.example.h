#pragma once

#include "WifiCredential.h"

namespace AppSecrets {

constexpr WifiCredential WifiCredentials[] = {
    {"YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD"},
    // {"ANOTHER_WIFI_SSID", "ANOTHER_WIFI_PASSWORD"},
};
constexpr size_t WifiCredentialCount = sizeof(WifiCredentials) / sizeof(WifiCredentials[0]);

constexpr const char* HubServerBaseURL = "http://192.168.1.2:8080/api/v1";
constexpr const char* HubServerApiKey = "YOUR_HUB_SERVER_API_KEY";

}  // namespace AppSecrets
