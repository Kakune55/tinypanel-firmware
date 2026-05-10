#pragma once

#include "RlcdDisplay.h"

struct DesktopClockUiModel;

class StatusBar {
 public:
  static constexpr int Height = 30;

  explicit StatusBar(RlcdDisplay& display);
  void draw(const DesktopClockUiModel& model);

 private:
  void drawSignalBars(int x, int y, bool connected, int rssi, bool black);

  RlcdDisplay& display_;
};
