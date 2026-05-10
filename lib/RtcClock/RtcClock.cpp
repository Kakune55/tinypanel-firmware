#include "RtcClock.h"

namespace {

constexpr uint8_t kControl1Reg = 0x00;
constexpr uint8_t kSecondsReg = 0x04;

}  // namespace

RtcClock::RtcClock(TwoWire& wire) : wire_(&wire) {}

bool RtcClock::begin() {
  uint8_t control = 0x00;
  return writeRegisters(kControl1Reg, &control, 1);
}

bool RtcClock::read(RtcDateTime& dateTime) {
  uint8_t data[7] = {};
  if (!readRegisters(kSecondsReg, data, sizeof(data))) {
    dateTime = {};
    return false;
  }

  const bool oscillatorStopped = (data[0] & 0x80) != 0;
  dateTime.second = bcdToDec(data[0] & 0x7F);
  dateTime.minute = bcdToDec(data[1] & 0x7F);
  dateTime.hour = bcdToDec(data[2] & 0x3F);
  dateTime.day = bcdToDec(data[3] & 0x3F);
  dateTime.weekday = data[4] & 0x07;
  dateTime.month = bcdToDec(data[5] & 0x1F);
  dateTime.year = 2000 + bcdToDec(data[6]);
  dateTime.valid = !oscillatorStopped;
  return dateTime.valid;
}

bool RtcClock::write(const RtcDateTime& dateTime) {
  uint8_t data[7] = {
      decToBcd(dateTime.second),
      decToBcd(dateTime.minute),
      decToBcd(dateTime.hour),
      decToBcd(dateTime.day),
      static_cast<uint8_t>(dateTime.weekday & 0x07),
      decToBcd(dateTime.month),
      decToBcd(static_cast<uint8_t>(dateTime.year >= 2000 ? dateTime.year - 2000 : 0)),
  };

  return writeRegisters(kSecondsReg, data, sizeof(data));
}

bool RtcClock::readRegisters(uint8_t reg, uint8_t* data, size_t len) {
  wire_->beginTransmission(Address);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {
    return false;
  }

  const size_t received = wire_->requestFrom(Address, static_cast<uint8_t>(len));
  if (received != len) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    data[i] = wire_->read();
  }

  return true;
}

bool RtcClock::writeRegisters(uint8_t reg, const uint8_t* data, size_t len) {
  wire_->beginTransmission(Address);
  wire_->write(reg);
  for (size_t i = 0; i < len; ++i) {
    wire_->write(data[i]);
  }
  return wire_->endTransmission() == 0;
}

uint8_t RtcClock::bcdToDec(uint8_t value) const {
  return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0F));
}

uint8_t RtcClock::decToBcd(uint8_t value) const {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}
