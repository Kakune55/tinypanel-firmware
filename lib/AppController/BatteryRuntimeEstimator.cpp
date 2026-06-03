#include "BatteryRuntimeEstimator.h"

namespace {

constexpr float kBatteryFullHoldPercent = 99.5f;

bool batteryInFullHold(const BatteryStatus& battery) {
  return battery.charging || battery.percentFloat >= kBatteryFullHoldPercent;
}

bool leapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint16_t daysBeforeMonth(int year, int month) {
  static constexpr uint16_t kDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  if (month < 1 || month > 12) {
    return 0;
  }
  return kDays[month - 1] + ((month > 2 && leapYear(year)) ? 1 : 0);
}

uint32_t absoluteMinute(const RtcDateTime& dt, uint32_t fallbackUptimeMs) {
  if (!dt.valid || dt.year < 2000) {
    return fallbackUptimeMs / 60000UL;
  }

  uint32_t days = 0;
  for (int year = 2000; year < dt.year; ++year) {
    days += leapYear(year) ? 366UL : 365UL;
  }
  days += daysBeforeMonth(dt.year, dt.month);
  days += static_cast<uint32_t>(dt.day > 0 ? dt.day - 1 : 0);
  return days * 1440UL + static_cast<uint32_t>(dt.hour) * 60UL + dt.minute;
}

}  // namespace

int BatteryRuntimeEstimator::etaMinutes() const {
  return batteryEtaMinutes_;
}

void BatteryRuntimeEstimator::resetEstimate(bool charging) {
  batteryHistoryCount_ = 0;
  batteryHistoryNext_ = 0;
  hasBatteryEtaFilter_ = false;
  hasBatteryEtaEstimate_ = false;
  batteryEtaWasCharging_ = charging;
  batteryEtaFilteredPercent_ = 0.0f;
  lastBatteryEtaSampleS_ = 0;
  batteryEtaMinutes_ = -1;
}

void BatteryRuntimeEstimator::updateEstimate(const BatteryStatus& battery, uint32_t nowS, uint32_t sampleIntervalMs) {
  constexpr float kFilterAlpha = 0.18f;
  constexpr float kEtaSmoothingAlpha = 0.25f;
  constexpr float kMaxSingleSampleDropPercent = 3.0f;
  constexpr float kMaxSingleSampleRisePercent = 1.0f;
  constexpr float kMaxDischargeRecoveryPercent = 0.12f;
  constexpr float kMinSlopePercentPerHour = 0.25f;
  constexpr float kMaxSlopePercentPerHour = 30.0f;
  constexpr uint32_t kMinBootAgeS = 20UL * 60UL;
  constexpr uint32_t kMinElapsedS = 35UL * 60UL;
  constexpr size_t kMinSamples = 8;
  constexpr float kMinEstimatedDropPercent = 1.0f;

  if (batteryInFullHold(battery) || battery.percentFloat <= 0.0f) {
    resetEstimate(batteryInFullHold(battery));
    return;
  }

  if (batteryEtaWasCharging_) {
    resetEstimate(false);
  }

  if (nowS < kMinBootAgeS) {
    return;
  }

  float filteredPercent = battery.percentFloat;
  if (hasBatteryEtaFilter_) {
    filteredPercent = batteryEtaFilteredPercent_ + kFilterAlpha * (battery.percentFloat - batteryEtaFilteredPercent_);
    if (filteredPercent > batteryEtaFilteredPercent_ + kMaxDischargeRecoveryPercent) {
      filteredPercent = batteryEtaFilteredPercent_ + kMaxDischargeRecoveryPercent;
    }
  } else {
    hasBatteryEtaFilter_ = true;
  }

  filteredPercent = constrain(filteredPercent, 0.0f, 100.0f);
  if (batteryHistoryCount_ > 0) {
    const BatteryHistoryPoint& previous =
        batteryHistory_[(batteryHistoryNext_ + BatteryHistorySize - 1) % BatteryHistorySize];
    const float sampleDelta = filteredPercent - previous.percent;
    if (-sampleDelta > kMaxSingleSampleDropPercent || sampleDelta > kMaxSingleSampleRisePercent) {
      return;
    }
  }
  batteryEtaFilteredPercent_ = filteredPercent;

  const uint32_t minSampleIntervalS = max<uint32_t>(1, sampleIntervalMs / 1000UL);
  if (lastBatteryEtaSampleS_ != 0 && nowS - lastBatteryEtaSampleS_ < minSampleIntervalS) {
    return;
  }
  lastBatteryEtaSampleS_ = nowS;

  batteryHistory_[batteryHistoryNext_].uptimeS = nowS;
  batteryHistory_[batteryHistoryNext_].percent = filteredPercent;
  batteryHistoryNext_ = (batteryHistoryNext_ + 1) % BatteryHistorySize;
  if (batteryHistoryCount_ < BatteryHistorySize) {
    ++batteryHistoryCount_;
  }

  if (batteryHistoryCount_ < kMinSamples) {
    return;
  }

  const size_t oldestIndex = batteryHistoryCount_ < BatteryHistorySize ? 0 : batteryHistoryNext_;
  const BatteryHistoryPoint& oldest = batteryHistory_[oldestIndex];
  const BatteryHistoryPoint& newest = batteryHistory_[(batteryHistoryNext_ + BatteryHistorySize - 1) % BatteryHistorySize];

  const uint32_t elapsedS = newest.uptimeS - oldest.uptimeS;
  if (elapsedS < kMinElapsedS) {
    return;
  }

  double sumT = 0.0;
  double sumP = 0.0;
  double sumTT = 0.0;
  double sumTP = 0.0;
  for (size_t i = 0; i < batteryHistoryCount_; ++i) {
    const size_t index = (oldestIndex + i) % BatteryHistorySize;
    const double t = static_cast<double>(batteryHistory_[index].uptimeS - oldest.uptimeS);
    const double p = static_cast<double>(batteryHistory_[index].percent);
    sumT += t;
    sumP += p;
    sumTT += t * t;
    sumTP += t * p;
  }

  const double n = static_cast<double>(batteryHistoryCount_);
  const double denominator = n * sumTT - sumT * sumT;
  if (denominator <= 0.0) {
    return;
  }

  const double slopePercentPerSecond = (n * sumTP - sumT * sumP) / denominator;
  const float dischargePercentPerHour = static_cast<float>(-slopePercentPerSecond * 3600.0);
  const float estimatedDropPercent = dischargePercentPerHour * elapsedS / 3600.0f;
  if (dischargePercentPerHour < kMinSlopePercentPerHour ||
      dischargePercentPerHour > kMaxSlopePercentPerHour ||
      estimatedDropPercent < kMinEstimatedDropPercent) {
    return;
  }

  const float etaMinutes = newest.percent / dischargePercentPerHour * 60.0f;
  if (etaMinutes > 0.0f && etaMinutes < 10000.0f) {
    if (hasBatteryEtaEstimate_ && batteryEtaMinutes_ >= 0) {
      const float smoothedEta =
          batteryEtaMinutes_ + kEtaSmoothingAlpha * (etaMinutes - static_cast<float>(batteryEtaMinutes_));
      batteryEtaMinutes_ = static_cast<int>(smoothedEta + 0.5f);
    } else {
      batteryEtaMinutes_ = static_cast<int>(etaMinutes + 0.5f);
      hasBatteryEtaEstimate_ = true;
    }
  }
}

bool BatteryRuntimeEstimator::appendChartSample(const BatteryStatus& battery,
                                                const RtcDateTime& now,
                                                uint32_t uptimeMs,
                                                uint32_t sampleIntervalMs) {
  const uint32_t nowMinute = absoluteMinute(now, uptimeMs);
  const uint32_t minIntervalMinutes = max<uint32_t>(1, sampleIntervalMs / 60000UL);

  if (batteryInFullHold(battery)) {
    batteryChartCount_ = 0;
    batteryChartStartMinute_ = 0;
    batteryChartLastAbsoluteMinute_ = nowMinute;
    return true;
  }

  if (batteryChartCount_ > 0 && nowMinute - batteryChartLastAbsoluteMinute_ < minIntervalMinutes) {
    return false;
  }

  if (batteryChartStartMinute_ == 0 || nowMinute < batteryChartStartMinute_) {
    batteryChartStartMinute_ = nowMinute;
    batteryChartCount_ = 0;
  }

  const uint32_t elapsed = nowMinute - batteryChartStartMinute_;
  if (elapsed > UINT16_MAX) {
    return false;
  }

  if (batteryChartCount_ >= BatteryChartSize) {
    const uint32_t removedMinutes = batteryChart_[1].minute;
    for (size_t i = 1; i < batteryChartCount_; ++i) {
      batteryChart_[i - 1] = batteryChart_[i];
      batteryChart_[i - 1].minute -= removedMinutes;
    }
    batteryChartStartMinute_ += removedMinutes;
    --batteryChartCount_;
  }

  const size_t index = batteryChartCount_++;
  batteryChart_[index].minute = static_cast<uint16_t>(elapsed);
  batteryChart_[index].percent = static_cast<uint8_t>(constrain(static_cast<int>(battery.percentFloat + 0.5f), 0, 100));
  batteryChartLastAbsoluteMinute_ = nowMinute;
  return true;
}

void BatteryRuntimeEstimator::restoreChart(const StoredBatteryHistoryPoint* points, size_t count) {
  batteryChartCount_ = 0;
  if (!points || count == 0) {
    batteryChartStartMinute_ = 0;
    batteryChartLastAbsoluteMinute_ = 0;
    return;
  }

  batteryChartStartMinute_ = points[0].absoluteMinute;
  batteryChartLastAbsoluteMinute_ = 0;
  for (size_t i = 0; i < count; ++i) {
    if (points[i].charging || points[i].absoluteMinute < batteryChartStartMinute_) {
      continue;
    }
    const uint32_t elapsed = points[i].absoluteMinute - batteryChartStartMinute_;
    if (elapsed > UINT16_MAX) {
      continue;
    }
    const size_t index = batteryChartCount_++;
    batteryChart_[index].minute = static_cast<uint16_t>(elapsed);
    batteryChart_[index].percent = static_cast<uint8_t>(constrain(static_cast<int>(points[i].percent + 0.5f), 0, 100));
    batteryChartLastAbsoluteMinute_ = points[i].absoluteMinute;
    if (batteryChartCount_ >= BatteryChartSize) {
      break;
    }
  }
}

void BatteryRuntimeEstimator::reset(bool charging) {
  resetEstimate(charging);
  batteryChartCount_ = 0;
  batteryChartStartMinute_ = 0;
  batteryChartLastAbsoluteMinute_ = 0;
}

void BatteryRuntimeEstimator::fillUiModel(DesktopClockUiModel& model) const {
  if (batteryChartCount_ == 0) {
    return;
  }

  model.batteryChart = batteryChart_;
  model.batteryChartCount = batteryChartCount_;
  model.batteryChartNowMinute = batteryChart_[batteryChartCount_ - 1].minute;
  model.batteryChartPredictedZeroMinute = model.batteryChartNowMinute;
  if (batteryEtaMinutes_ > 0) {
    const uint32_t predicted = static_cast<uint32_t>(model.batteryChartNowMinute) +
                               static_cast<uint32_t>(batteryEtaMinutes_);
    model.batteryChartPredictedZeroMinute = static_cast<uint16_t>(min<uint32_t>(UINT16_MAX, predicted));
  }
}
