#include "I2cScanner.h"

I2cScanner::I2cScanner(TwoWire& wire) : wire_(&wire) {}

void I2cScanner::begin(int sda, int scl, uint32_t frequency) {
  wire_->begin(sda, scl);
  wire_->setClock(frequency);
}

bool I2cScanner::ping(uint8_t address) const {
  wire_->beginTransmission(address);
  return wire_->endTransmission() == 0;
}

int I2cScanner::scan(uint8_t* addresses, int maxAddresses) const {
  int count = 0;

  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (!ping(addr)) {
      continue;
    }

    if (addresses != nullptr && count < maxAddresses) {
      addresses[count] = addr;
    }
    ++count;
  }

  return count;
}

void I2cScanner::printScan(Stream& out) const {
  out.println("I2C scan start");

  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (ping(addr)) {
      out.printf("I2C device found: 0x%02X\n", addr);
    }
  }

  out.println("I2C scan done");
}
