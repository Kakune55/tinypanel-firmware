#pragma once

#include <Arduino.h>

struct BatteryStatus {
  int rawAdc = 0;
  uint32_t rawVoltageMv = 0;
  float voltage = 0.0f;
  int percent = 0;
  bool charging = false;
  bool low = false;
  bool critical = false;
};

class BatteryMonitor {
public:
  bool begin();
  int readRawAdc(int samples = 32) const;
  float readRawVoltage(int samples = 32) const;
  float readVoltage(int samples = 32) const;
  int percentFromRawAdc(int rawAdc) const;
  BatteryStatus readStatus(int samples = 32) const;

private:
  bool updateChargingState(int rawAdc) const;

  mutable bool hasLastRawAdc_ = false;
  mutable int lastRawAdc_ = 0;
  mutable int risingSamples_ = 0;
  mutable int fallingSamples_ = 0;
  mutable bool charging_ = false;
};
