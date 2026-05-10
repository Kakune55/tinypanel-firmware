#pragma once

#include "DesktopClockTypes.h"
#include "RlcdDisplay.h"
#include "StatusBar.h"

class DesktopClockUi {
 public:
  explicit DesktopClockUi(RlcdDisplay& display);

  void render(const DesktopClockUiModel& model);
  static DesktopClockPage nextPage(DesktopClockPage page);

 private:
  RlcdDisplay& display_;
  StatusBar statusBar_;
};
