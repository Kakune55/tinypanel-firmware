#pragma once

#include <Arduino.h>
#include <Wire.h>

struct RtcDateTime {
  uint16_t year = 2000;
  uint8_t month = 1;
  uint8_t day = 1;
  uint8_t weekday = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  bool valid = false;
};

class RtcClock {
public:
  static constexpr uint8_t Address = 0x51;

  explicit RtcClock(TwoWire& wire = Wire);

  bool begin();
  bool read(RtcDateTime& dateTime);
  bool write(const RtcDateTime& dateTime);

private:
  bool readRegisters(uint8_t reg, uint8_t* data, size_t len);
  bool writeRegisters(uint8_t reg, const uint8_t* data, size_t len);
  uint8_t bcdToDec(uint8_t value) const;
  uint8_t decToBcd(uint8_t value) const;

  TwoWire* wire_;
};
