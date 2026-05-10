#include "DesktopClockUi.h"

#include "StatusBar.h"
#include "UiDraw.h"

namespace {

const char* weekdayName(uint8_t weekday) {
  static constexpr const char* kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return weekday < 7 ? kNames[weekday] : "---";
}

void formatDate(char* out, size_t len, const RtcDateTime& dt) {
  if (!dt.valid) {
    snprintf(out, len, "RTC NOT SET");
    return;
  }
  snprintf(out, len, "%04u-%02u-%02u %s", dt.year, dt.month, dt.day, weekdayName(dt.weekday));
}

void formatTimeWithSeconds(char* out, size_t len, const RtcDateTime& dt) {
  if (!dt.valid) {
    snprintf(out, len, "--:--:--");
    return;
  }
  snprintf(out, len, "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
}

int dayProgressPercent(const RtcDateTime& dt) {
  if (!dt.valid) {
    return 0;
  }
  const int minutes = dt.hour * 60 + dt.minute;
  return UiDraw::clampInt(minutes * 100 / 1440, 0, 100);
}

void drawPageDots(RlcdDisplay& display, DesktopClockPage page) {
  const int y = 283;
  for (int i = 0; i < 3; ++i) {
    int x = 184 + i * 16;
    bool active = static_cast<int>(page) == i;
    if (active) {
      display.fillCircle(x, y, 4, true);
    } else {
      display.drawCircle(x, y, 4, true);
    }
  }
}

void drawMetricCard(RlcdDisplay& display, int x, int y, int w, int h, const char* label, const char* value, const char* note) {
  display.drawRoundRect(x, y, w, h, 7, true);
  display.drawText(x + 12, y + 10, label, true, 1);
  display.drawText(x + 12, y + 28, value, true, 2);
  if (note && note[0]) {
    display.drawText(x + 12, y + h - 18, note, true, 1);
  }
}

void drawClockPage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  char text[40];

  display.clear(true);
  statusBar.draw(model);

  formatTimeWithSeconds(text, sizeof(text), model.now);
  display.drawText(20, 56, text, true, 5);

  formatDate(text, sizeof(text), model.now);
  display.drawText(32, 116, text, true, 2);

  snprintf(text, sizeof(text), "DAY %d%%", dayProgressPercent(model.now));
  display.drawText(32, 152, text, true, 1);
  UiDraw::progressBar(display, 112, 149, 252, 14, dayProgressPercent(model.now));

  char temp[24];
  char hum[24];
  if (model.environment.valid) {
    snprintf(temp, sizeof(temp), "%.1fC", model.environment.temperatureC);
    snprintf(hum, sizeof(hum), "%.0f%%", model.environment.humidityRh);
  } else {
    snprintf(temp, sizeof(temp), "--");
    snprintf(hum, sizeof(hum), "--");
  }

  drawMetricCard(display, 24, 184, 108, 68, "TEMP", temp, "SHTC3");
  drawMetricCard(display, 146, 184, 108, 68, "HUM", hum, "ROOM");

  display.drawRoundRect(268, 184, 108, 68, 7, true);
  display.drawText(280, 194, "BATTERY", true, 1);
  UiDraw::progressBar(display, 282, 216, 74, 12, model.battery.percent);
  snprintf(text, sizeof(text), "%d%%", model.battery.percent);
  display.drawText(362, 218, text, true, 1);
  snprintf(text, sizeof(text), "%.2fV", model.battery.voltage);
  display.drawText(282, 238, text, true, 1);

  display.drawText(24, 270, "KEY REFRESH", true, 1);
  display.drawText(286, 270, "BOOT NEXT", true, 1);
  drawPageDots(display, model.page);
}

void drawDashboardPage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  char text[48];

  display.clear(true);
  statusBar.draw(model);

  display.drawText(18, 48, "TODAY", true, 4);
  display.drawFastHLine(18, 88, 364, true);

  snprintf(text, sizeof(text), "UPTIME %luh %lum",
           static_cast<unsigned long>(model.uptimeMs / 3600000UL),
           static_cast<unsigned long>((model.uptimeMs / 60000UL) % 60UL));
  display.drawText(24, 106, text, true, 1);

  snprintf(text, sizeof(text), "TIME SOURCE %s", model.ntpSynced ? "NTP+RTC" : "RTC");
  display.drawText(24, 128, text, true, 1);

  snprintf(text, sizeof(text), "WIFI %s", model.wifiConnected ? model.wifiIp.c_str() : "OFFLINE");
  display.drawText(24, 150, text, true, 1);

  snprintf(text, sizeof(text), "SD %s", model.sdMounted ? "READY" : "NO CARD");
  display.drawText(24, 172, text, true, 1);

  display.drawText(24, 204, "DAY", true, 1);
  UiDraw::progressBar(display, 72, 201, 292, 14, dayProgressPercent(model.now));

  display.drawText(24, 232, "BAT", true, 1);
  UiDraw::progressBar(display, 72, 229, 292, 14, model.battery.percent);

  display.drawText(24, 270, "KEY SYNC", true, 1);
  display.drawText(286, 270, "BOOT NEXT", true, 1);
  drawPageDots(display, model.page);
}

void drawSystemPage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  char text[48];

  display.clear(true);
  statusBar.draw(model);
  display.drawText(18, 48, "SYSTEM", true, 4);
  display.drawFastHLine(18, 88, 364, true);

  snprintf(text, sizeof(text), "IP %s", model.wifiConnected ? model.wifiIp.c_str() : "--");
  display.drawText(24, 112, text, true, 1);

  snprintf(text, sizeof(text), "SSID %s", model.wifiSsid.c_str());
  display.drawText(24, 132, text, true, 1);

  snprintf(text, sizeof(text), "NTP %s", model.ntpSynced ? "SYNCED" : "PENDING");
  display.drawText(24, 152, text, true, 1);

  snprintf(text, sizeof(text), "SD %s", model.sdMounted ? "READY" : model.sdStatus);
  display.drawText(24, 172, text, true, 1);

  snprintf(text, sizeof(text), "HEAP %lu", static_cast<unsigned long>(model.freeHeap));
  display.drawText(24, 192, text, true, 1);

  snprintf(text, sizeof(text), "PSRAM %lu", static_cast<unsigned long>(model.freePsram));
  display.drawText(24, 212, text, true, 1);

  snprintf(text, sizeof(text), "BAT %.3fV %d%%", model.battery.voltage, model.battery.percent);
  display.drawText(24, 232, text, true, 1);

  if (model.environment.valid) {
    snprintf(text, sizeof(text), "ENV %.1fC %.0f%%", model.environment.temperatureC, model.environment.humidityRh);
    display.drawText(24, 252, text, true, 1);
  }

  display.drawText(24, 270, "KEY SYNC", true, 1);
  display.drawText(286, 270, "BOOT NEXT", true, 1);
  drawPageDots(display, model.page);
}

}  // namespace

DesktopClockUi::DesktopClockUi(RlcdDisplay& display) : display_(display), statusBar_(display) {}

void DesktopClockUi::render(const DesktopClockUiModel& model) {
  switch (model.page) {
    case DesktopClockPage::Clock:
      drawClockPage(display_, statusBar_, model);
      break;
    case DesktopClockPage::Dashboard:
      drawDashboardPage(display_, statusBar_, model);
      break;
    case DesktopClockPage::System:
      drawSystemPage(display_, statusBar_, model);
      break;
  }
  display_.flushFull();
}

DesktopClockPage DesktopClockUi::nextPage(DesktopClockPage page) {
  return static_cast<DesktopClockPage>((static_cast<int>(page) + 1) % 3);
}
