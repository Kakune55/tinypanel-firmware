#pragma once

#include <Arduino.h>

#include "RtcClock.h"

class TimeSync {
public:
  bool begin(const char* timezone, const char* ntp1 = "pool.ntp.org", const char* ntp2 = "time.nist.gov");
  bool syncToRtc(RtcClock& rtc, uint32_t timeoutMs = 15000);
  bool getLocalRtcDateTime(RtcDateTime& dateTime) const;
};
