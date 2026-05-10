#include "BatteryMonitor.h"

#include <algorithm>

#include "BoardConfig.h"

namespace {

struct BatteryCurvePoint {
  float voltage;
  int percent;
};

constexpr BatteryCurvePoint kBatteryCurve[] = {
    {4.20f, 100},
    {4.10f, 90},
    {4.00f, 80},
    {3.92f, 70},
    {3.85f, 60},
    {3.79f, 50},
    {3.72f, 40},
    {3.65f, 30},
    {3.58f, 20},
    {3.45f, 10},
    {3.30f, 0},
};

}  // namespace

bool BatteryMonitor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(BoardConfig::BatteryAdcPin, ADC_11db);
  return true;
}

int BatteryMonitor::readRawAdc(int samples) const {
  samples = std::max(1, samples);
  uint32_t sum = 0;

  for (int i = 0; i < samples; ++i) {
    sum += analogRead(BoardConfig::BatteryAdcPin);
    delay(2);
  }

  return static_cast<int>((sum + samples / 2) / samples);
}

float BatteryMonitor::readRawVoltage(int samples) const {
  samples = std::max(1, samples);
  uint32_t sumMv = 0;

  for (int i = 0; i < samples; ++i) {
    sumMv += analogReadMilliVolts(BoardConfig::BatteryAdcPin);
    delay(2);
  }

  const float adcMv = sumMv / static_cast<float>(samples);
  return adcMv / 1000.0f * BoardConfig::BatteryDividerRatio;
}

float BatteryMonitor::readVoltage(int samples) const {
  return readRawVoltage(samples) * BoardConfig::BatteryVoltageCalibration + BoardConfig::BatteryVoltageOffset;
}

int BatteryMonitor::percentFromVoltage(float voltage) const {
  if (voltage >= kBatteryCurve[0].voltage) {
    return 100;
  }

  constexpr size_t lastIndex = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]) - 1;
  if (voltage <= kBatteryCurve[lastIndex].voltage) {
    return 0;
  }

  for (size_t i = 0; i < lastIndex; ++i) {
    const auto high = kBatteryCurve[i];
    const auto low = kBatteryCurve[i + 1];
    if (voltage <= high.voltage && voltage >= low.voltage) {
      const float t = (voltage - low.voltage) / (high.voltage - low.voltage);
      return static_cast<int>(low.percent + t * (high.percent - low.percent) + 0.5f);
    }
  }

  return 0;
}

BatteryStatus BatteryMonitor::readStatus(int samples) const {
  BatteryStatus status;
  samples = std::max(1, samples);
  status.rawAdc = readRawAdc(samples);
  const float rawVoltage = readRawVoltage(samples);
  status.rawVoltageMv = static_cast<uint32_t>(rawVoltage * 1000.0f + 0.5f);
  status.voltage = rawVoltage * BoardConfig::BatteryVoltageCalibration + BoardConfig::BatteryVoltageOffset;
  status.percent = percentFromVoltage(status.voltage);
  status.low = status.percent <= 20;
  status.critical = status.percent <= 10;
  return status;
}
