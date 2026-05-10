#include "TimeSync.h"

#include <ctime>

#include <WiFi.h>

bool TimeSync::begin(const char* timezone, const char* ntp1, const char* ntp2) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  configTzTime(timezone, ntp1, ntp2);
  return true;
}

bool TimeSync::syncToRtc(RtcClock& rtc, uint32_t timeoutMs) {
  RtcDateTime dateTime;
  const uint32_t startMs = millis();

  while (millis() - startMs < timeoutMs) {
    if (getLocalRtcDateTime(dateTime)) {
      if (rtc.write(dateTime)) {
        Serial.printf("NTP: RTC updated %04u-%02u-%02u %02u:%02u:%02u\n",
                      dateTime.year,
                      dateTime.month,
                      dateTime.day,
                      dateTime.hour,
                      dateTime.minute,
                      dateTime.second);
        return true;
      }
      Serial.println("NTP: RTC write failed");
      return false;
    }
    delay(250);
  }

  Serial.println("NTP: timeout");
  return false;
}

bool TimeSync::getLocalRtcDateTime(RtcDateTime& dateTime) const {
  tm timeInfo = {};
  if (!getLocalTime(&timeInfo, 10)) {
    dateTime = {};
    return false;
  }

  dateTime.year = static_cast<uint16_t>(timeInfo.tm_year + 1900);
  dateTime.month = static_cast<uint8_t>(timeInfo.tm_mon + 1);
  dateTime.day = static_cast<uint8_t>(timeInfo.tm_mday);
  dateTime.weekday = static_cast<uint8_t>(timeInfo.tm_wday);
  dateTime.hour = static_cast<uint8_t>(timeInfo.tm_hour);
  dateTime.minute = static_cast<uint8_t>(timeInfo.tm_min);
  dateTime.second = static_cast<uint8_t>(timeInfo.tm_sec);
  dateTime.valid = true;
  return true;
}
