#include "StatusBar.h"

#include <cstring>

#include "DesktopClockTypes.h"
#include "PixelIcons.h"
#include "UiDraw.h"

namespace {

const char* pageName(DesktopClockPage page) {
  switch (page) {
    case DesktopClockPage::Clock:
      return "CLOCK";
    case DesktopClockPage::Message:
      return "MESSAGE";
    case DesktopClockPage::Todo:
      return "TODO";
    case DesktopClockPage::Weather:
      return "WEATHER";
    case DesktopClockPage::System:
      return "SYSTEM";
  }
  return "---";
}

}  // namespace

StatusBar::StatusBar(RlcdDisplay& display) : display_(display) {}

void StatusBar::draw(const DesktopClockUiModel& model) {
  char text[32];
  constexpr bool kInk = false;

  display_.fillRect(0, 0, display_.width(), Height, true);
  display_.drawText(8, 8, pageName(model.page), kInk, 2);

  snprintf(text, sizeof(text), "%d%%", model.battery.percent);
  const int batteryScale = 2;
  const int batteryIconWidth = PixelIcons::BatteryTemplate.width * batteryScale;
  const int percentWidth = static_cast<int>(std::strlen(text)) * 6 * batteryScale;
  const int rightMargin = 8;
  const int gap = 2;
  const int groupGap = 8;
  const int batteryX = display_.width() - rightMargin - batteryIconWidth;
  const int percentX = batteryX - gap - percentWidth;

  display_.drawText(percentX, 8, text, kInk, batteryScale);
  UiDraw::batteryIcon(display_, batteryX, 8, model.battery, batteryScale, kInk);

  int cursorX = percentX - groupGap;
  if (model.wifiConnected) {
    snprintf(text, sizeof(text), "%d", model.wifiRssi);
  } else {
    snprintf(text, sizeof(text), "OFF");
  }
  const int wifiTextWidth = static_cast<int>(std::strlen(text)) * 6;
  const int wifiBarsWidth = 22;
  const int wifiX = cursorX - wifiTextWidth;
  display_.drawText(wifiX, 11, text, kInk, 1);
  drawSignalBars(wifiX - gap - wifiBarsWidth, 8, model.wifiConnected, model.wifiRssi, kInk);

  cursorX = wifiX - gap - wifiBarsWidth - groupGap;
  if (model.hubSyncing || model.hubSyncFailed) {
    const PixelIcons::Bitmap& hubIcon = model.hubSyncing ? PixelIcons::ServerSync : PixelIcons::ServerOffline;
    UiDraw::bitmapScaled(display_, cursorX - hubIcon.width, 8, hubIcon, 1, kInk);
    cursorX -= hubIcon.width + groupGap;
  }

  if (model.ntpSyncing || model.ntpSyncFailed) {
    const PixelIcons::Bitmap& serverIcon = model.ntpSyncing ? PixelIcons::ServerSync : PixelIcons::ServerOffline;
    UiDraw::bitmapScaled(display_, cursorX - serverIcon.width, 8, serverIcon, 1, kInk);
  }
}

void StatusBar::drawSignalBars(int x, int y, bool connected, int rssi, bool black) {
  int bars = 0;
  if (connected) {
    if (rssi > -60) {
      bars = 4;
    } else if (rssi > -70) {
      bars = 3;
    } else if (rssi > -80) {
      bars = 2;
    } else {
      bars = 1;
    }
  }

  for (int i = 0; i < 4; ++i) {
    int h = 4 + i * 3;
    int bx = x + i * 6;
    int by = y + 14 - h;
    if (i < bars) {
      display_.fillRect(bx, by, 4, h, black);
    } else {
      display_.drawRect(bx, by, 4, h, black);
    }
  }
}
