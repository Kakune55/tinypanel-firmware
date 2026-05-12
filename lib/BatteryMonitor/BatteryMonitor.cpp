#include "BatteryMonitor.h"

#include <algorithm>

#include "BoardConfig.h"

namespace {

struct BatteryCurvePoint {
  int rawAdc;
  float percent;
};

constexpr BatteryCurvePoint kBatteryCurve[] = {
    {1655, 100.00f},
    {1650, 99.90f},
    {1645, 99.77f},
    {1640, 98.62f},
    {1635, 97.50f},
    {1630, 96.32f},
    {1625, 94.44f},
    {1620, 92.76f},
    {1615, 90.67f},
    {1610, 88.59f},
    {1605, 86.91f},
    {1600, 85.80f},
    {1595, 83.99f},
    {1590, 82.11f},
    {1585, 80.86f},
    {1580, 79.60f},
    {1575, 77.72f},
    {1570, 76.99f},
    {1565, 75.84f},
    {1560, 74.37f},
    {1555, 72.29f},
    {1550, 70.40f},
    {1545, 68.94f},
    {1540, 67.69f},
    {1535, 66.71f},
    {1530, 65.81f},
    {1525, 64.14f},
    {1520, 62.81f},
    {1515, 61.00f},
    {1510, 59.96f},
    {1505, 58.49f},
    {1500, 57.24f},
    {1495, 55.57f},
    {1490, 54.32f},
    {1485, 53.06f},
    {1480, 51.39f},
    {1475, 50.57f},
    {1470, 49.32f},
    {1465, 48.07f},
    {1460, 46.40f},
    {1455, 45.56f},
    {1450, 44.10f},
    {1445, 42.64f},
    {1440, 41.59f},
    {1435, 39.71f},
    {1430, 37.62f},
    {1425, 35.74f},
    {1420, 34.28f},
    {1415, 32.40f},
    {1410, 31.25f},
    {1405, 30.10f},
    {1400, 28.43f},
    {1395, 27.17f},
    {1390, 25.71f},
    {1385, 24.46f},
    {1380, 23.41f},
    {1375, 22.16f},
    {1370, 20.69f},
    {1365, 19.44f},
    {1360, 18.26f},
    {1355, 17.14f},
    {1350, 15.89f},
    {1345, 15.22f},
    {1340, 14.42f},
    {1335, 12.96f},
    {1330, 11.71f},
    {1325, 10.73f},
    {1320, 9.62f},
    {1315, 8.78f},
    {1310, 8.15f},
    {1305, 7.42f},
    {1300, 6.94f},
    {1295, 6.55f},
    {1290, 6.17f},
    {1285, 5.77f},
    {1280, 5.56f},
    {1275, 5.23f},
    {1270, 4.77f},
    {1265, 4.42f},
    {1260, 4.08f},
    {1255, 3.66f},
    {1250, 3.40f},
    {1245, 3.11f},
    {1240, 2.81f},
    {1235, 2.51f},
    {1230, 2.28f},
    {1225, 2.05f},
    {1220, 1.84f},
    {1215, 1.63f},
    {1210, 1.39f},
    {1205, 1.16f},
    {1200, 0.93f},
    {1195, 0.70f},
    {1190, 0.46f},
    {1185, 0.27f},
    {1180, 0.08f},
    {1175, 0.00f},
};

constexpr int kPlugInAdcStep = 20;
constexpr int kChargingRiseAdc = 3;
constexpr int kChargingFallAdc = 3;
constexpr int kChargingRiseSamples = 2;
constexpr int kChargingFallSamples = 2;

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

float BatteryMonitor::percentFromRawAdc(int rawAdc) const {
  if (rawAdc >= kBatteryCurve[0].rawAdc) {
    return 100.0f;
  }

  constexpr size_t lastIndex = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]) - 1;
  if (rawAdc <= kBatteryCurve[lastIndex].rawAdc) {
    return 0.0f;
  }

  for (size_t i = 0; i < lastIndex; ++i) {
    const auto high = kBatteryCurve[i];
    const auto low = kBatteryCurve[i + 1];
    if (rawAdc <= high.rawAdc && rawAdc >= low.rawAdc) {
      const float t = (rawAdc - low.rawAdc) / static_cast<float>(high.rawAdc - low.rawAdc);
      return low.percent + t * (high.percent - low.percent);
    }
  }

  return 0.0f;
}

bool BatteryMonitor::updateChargingState(int rawAdc) const {
  if (!hasLastRawAdc_) {
    hasLastRawAdc_ = true;
    lastRawAdc_ = rawAdc;
    return charging_;
  }

  const int delta = rawAdc - lastRawAdc_;
  lastRawAdc_ = rawAdc;

  if (delta >= kPlugInAdcStep) {
    risingSamples_ = kChargingRiseSamples;
    fallingSamples_ = 0;
    charging_ = true;
    return charging_;
  }

  if (delta <= -kPlugInAdcStep) {
    risingSamples_ = 0;
    fallingSamples_ = kChargingFallSamples;
    charging_ = false;
    return charging_;
  }

  if (delta >= kChargingRiseAdc) {
    ++risingSamples_;
    fallingSamples_ = 0;
    if (risingSamples_ >= kChargingRiseSamples) {
      charging_ = true;
    }
  } else if (delta <= -kChargingFallAdc) {
    risingSamples_ = 0;
    ++fallingSamples_;
    if (fallingSamples_ >= kChargingFallSamples) {
      charging_ = false;
    }
  }

  return charging_;
}

BatteryStatus BatteryMonitor::readStatus(int samples) const {
  BatteryStatus status;
  samples = std::max(1, samples);
  status.rawAdc = readRawAdc(samples);
  const float rawVoltage = readRawVoltage(samples);
  status.rawVoltageMv = static_cast<uint32_t>(rawVoltage * 1000.0f + 0.5f);
  status.voltage = rawVoltage * BoardConfig::BatteryVoltageCalibration + BoardConfig::BatteryVoltageOffset;
  status.percentFloat = percentFromRawAdc(status.rawAdc);
  status.percent = static_cast<int>(status.percentFloat + 0.5f);
  status.charging = updateChargingState(status.rawAdc);
  status.low = status.percentFloat <= 20.0f;
  status.critical = status.percentFloat <= 10.0f;
  return status;
}
