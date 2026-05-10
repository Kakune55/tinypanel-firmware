#pragma once

#include <Arduino.h>
#include <Wire.h>

class I2cScanner {
public:
  explicit I2cScanner(TwoWire& wire = Wire);

  void begin(int sda, int scl, uint32_t frequency = 400000);
  bool ping(uint8_t address) const;
  int scan(uint8_t* addresses, int maxAddresses) const;
  void printScan(Stream& out = Serial) const;

private:
  TwoWire* wire_;
};
