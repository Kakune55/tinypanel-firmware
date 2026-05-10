#pragma once

#include <Arduino.h>
#include <Wire.h>

struct Shtc3Reading {
  float temperatureC = 0.0f;
  float humidityRh = 0.0f;
  bool valid = false;
};

class Shtc3Sensor {
public:
  static constexpr uint8_t Address = 0x70;

  explicit Shtc3Sensor(TwoWire& wire = Wire);

  bool begin();
  bool read(Shtc3Reading& reading);

private:
  bool writeCommand(uint16_t command);
  bool readBytes(uint8_t* data, size_t len);
  uint8_t crc8(const uint8_t* data, size_t len) const;

  TwoWire* wire_;
};
