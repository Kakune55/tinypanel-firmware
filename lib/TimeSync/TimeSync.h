#pragma once

#include <Arduino.h>

#include "RtcClock.h"

class TimeSync {
public:
  bool begin(const char* timezone,
             const char* ntp1 = "ntp1.ntsc.ac.cn",
             const char* ntp2 = "ntp1.cas.cn",
             const char* ntp3 = "cn.pool.ntp.org");
  bool syncToRtc(RtcClock& rtc, uint32_t timeoutMs = 15000);
  bool waitForTime(RtcDateTime& dateTime, uint32_t timeoutMs = 15000);
  bool getLocalRtcDateTime(RtcDateTime& dateTime) const;
};
