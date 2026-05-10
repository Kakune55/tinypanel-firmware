#pragma once

#include <Arduino.h>

struct BatteryStatus {
  int rawAdc = 0;
  uint32_t rawVoltageMv = 0;
  float voltage = 0.0f;
  int percent = 0;
  bool low = false;
  bool critical = false;
};

class BatteryMonitor {
public:
  bool begin();
  int readRawAdc(int samples = 32) const;
  float readRawVoltage(int samples = 32) const;
  float readVoltage(int samples = 32) const;
  int percentFromVoltage(float voltage) const;
  BatteryStatus readStatus(int samples = 32) const;
};
