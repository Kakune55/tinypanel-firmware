#pragma once

#include <Arduino.h>

#include "AppStorage.h"
#include "BatteryMonitor.h"
#include "DesktopClockTypes.h"
#include "RtcClock.h"

class BatteryRuntimeEstimator {
 public:
  static constexpr size_t BatteryHistorySize = 180;
  static constexpr size_t BatteryChartSize = AppStorage::MaxBatteryHistoryPoints;

  int etaMinutes() const;
  void updateEstimate(const BatteryStatus& battery, uint32_t nowS, uint32_t sampleIntervalMs);
  bool appendChartSample(const BatteryStatus& battery,
                         const RtcDateTime& now,
                         uint32_t uptimeMs,
                         uint32_t sampleIntervalMs);
  void restoreChart(const StoredBatteryHistoryPoint* points, size_t count);
  void reset(bool charging);
  void fillUiModel(DesktopClockUiModel& model) const;

 private:
  struct BatteryHistoryPoint {
    uint32_t uptimeS = 0;
    float percent = 0.0f;
  };

  void resetEstimate(bool charging);

  BatteryHistoryPoint batteryHistory_[BatteryHistorySize];
  size_t batteryHistoryCount_ = 0;
  size_t batteryHistoryNext_ = 0;
  int batteryEtaMinutes_ = -1;
  bool hasBatteryEtaEstimate_ = false;
  bool hasBatteryEtaFilter_ = false;
  bool batteryEtaWasCharging_ = false;
  float batteryEtaFilteredPercent_ = 0.0f;
  uint32_t lastBatteryEtaSampleS_ = 0;
  BatteryChartPoint batteryChart_[BatteryChartSize];
  size_t batteryChartCount_ = 0;
  uint32_t batteryChartStartMinute_ = 0;
  uint32_t batteryChartLastAbsoluteMinute_ = 0;
};
