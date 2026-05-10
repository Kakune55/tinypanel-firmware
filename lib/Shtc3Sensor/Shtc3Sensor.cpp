#include "Shtc3Sensor.h"

namespace {

constexpr uint16_t kWakeCommand = 0x3517;
constexpr uint16_t kSleepCommand = 0xB098;
constexpr uint16_t kMeasureCommand = 0x7CA2;

}  // namespace

Shtc3Sensor::Shtc3Sensor(TwoWire& wire) : wire_(&wire) {}

bool Shtc3Sensor::begin() {
  if (!writeCommand(kWakeCommand)) {
    return false;
  }
  delay(2);
  writeCommand(kSleepCommand);
  return true;
}

bool Shtc3Sensor::read(Shtc3Reading& reading) {
  reading = {};

  if (!writeCommand(kWakeCommand)) {
    return false;
  }
  delay(2);

  if (!writeCommand(kMeasureCommand)) {
    writeCommand(kSleepCommand);
    return false;
  }

  delay(15);

  uint8_t data[6] = {};
  if (!readBytes(data, sizeof(data))) {
    writeCommand(kSleepCommand);
    return false;
  }

  writeCommand(kSleepCommand);

  if (crc8(data, 2) != data[2] || crc8(data + 3, 2) != data[5]) {
    return false;
  }

  const uint16_t rawTemp = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  const uint16_t rawHumidity = (static_cast<uint16_t>(data[3]) << 8) | data[4];
  reading.temperatureC = -45.0f + 175.0f * rawTemp / 65535.0f;
  reading.humidityRh = 100.0f * rawHumidity / 65535.0f;
  reading.valid = true;
  return true;
}

bool Shtc3Sensor::writeCommand(uint16_t command) {
  wire_->beginTransmission(Address);
  wire_->write(static_cast<uint8_t>(command >> 8));
  wire_->write(static_cast<uint8_t>(command & 0xFF));
  return wire_->endTransmission() == 0;
}

bool Shtc3Sensor::readBytes(uint8_t* data, size_t len) {
  const size_t received = wire_->requestFrom(Address, static_cast<uint8_t>(len));
  if (received != len) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    data[i] = wire_->read();
  }

  return true;
}

uint8_t Shtc3Sensor::crc8(const uint8_t* data, size_t len) const {
  uint8_t crc = 0xFF;

  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }

  return crc;
}
