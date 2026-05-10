#include "DesktopClockUi.h"

#include "StatusBar.h"
#include "UiDraw.h"

namespace {

const char* weekdayName(uint8_t weekday) {
  static constexpr const char* kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return weekday < 7 ? kNames[weekday] : "---";
}

const char* messageTitle(const HubMessage& message) {
  return message.author.length() > 0 ? message.author.c_str() : "anonymous";
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
  for (int i = 0; i < 4; ++i) {
    int x = 176 + i * 16;
    bool active = static_cast<int>(page) == i;
    if (active) {
      display.fillCircle(x, y, 4, true);
    } else {
      display.drawCircle(x, y, 4, true);
    }
  }
}

void drawClippedText(RlcdDisplay& display, int x, int y, const String& text, int maxChars, bool black = true, int scale = 1) {
  char buffer[48];
  const int limit = min(maxChars, static_cast<int>(sizeof(buffer) - 1));
  int out = 0;
  for (int i = 0; i < static_cast<int>(text.length()) && out < limit; ++i) {
    const char c = text[i];
    buffer[out++] = (c == '\n' || c == '\r') ? ' ' : c;
  }
  buffer[out] = '\0';
  display.drawText(x, y, buffer, black, scale);
}

String formatMessageTimestamp(const String& value) {
  if (value.length() >= 19 && value[10] == 'T') {
    return value.substring(0, 10) + " " + value.substring(11, 19);
  }
  return value;
}

void drawWrappedText(RlcdDisplay& display,
                     int x,
                     int y,
                     int maxCharsPerLine,
                     int maxLines,
                     const String& text,
                     uint16_t scrollLine,
                     bool black = true,
                     int scale = 1,
                     int lineHeight = 14) {
  char line[40];
  int lineLen = 0;
  uint16_t logicalLine = 0;
  int drawnLines = 0;

  auto flushLine = [&]() {
    if (logicalLine >= scrollLine && drawnLines < maxLines) {
      line[lineLen] = '\0';
      display.drawText(x, y + drawnLines * lineHeight, line, black, scale);
      ++drawnLines;
    }
    ++logicalLine;
    lineLen = 0;
  };

  for (size_t i = 0; i < text.length() && drawnLines < maxLines; ++i) {
    const char c = text[i];
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      flushLine();
      continue;
    }
    line[lineLen++] = c;
    if (lineLen >= maxCharsPerLine || lineLen >= static_cast<int>(sizeof(line) - 1)) {
      flushLine();
    }
  }
  if (lineLen > 0 && drawnLines < maxLines) {
    flushLine();
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

void drawMessagePage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  display.clear(true);
  statusBar.draw(model);

  constexpr int topY = 38;
  constexpr int bottomY = 262;
  constexpr int splitX = 142;
  constexpr int listX = 10;
  constexpr int listW = 124;
  constexpr int bodyX = 154;
  constexpr int bodyW = 236;

  display.drawFastVLine(splitX, topY, bottomY - topY, true);
  if (!model.messageBodyFocused) {
    display.drawFastHLine(listX, topY, listW, true);
    display.drawFastHLine(listX, topY + 2, listW, true);
  } else {
    display.drawFastHLine(bodyX, topY, bodyW, true);
    display.drawFastHLine(bodyX, topY + 2, bodyW, true);
  }

  if (!model.messages || model.messageCount == 0) {
    display.drawText(listX + 8, topY + 24, "NO MSG", true, 1);
    display.drawText(bodyX + 8, topY + 24, "No messages", true, 1);
    display.drawText(24, 270, "KEY SELECT", true, 1);
    display.drawText(250, 270, "DBL FOCUS", true, 1);
    drawPageDots(display, model.page);
    return;
  }

  const size_t selected = min(model.selectedMessage, model.messageCount - 1);
  constexpr int itemY = topY + 14;
  constexpr int itemH = 30;
  for (size_t i = 0; i < model.messageCount && i < 7; ++i) {
    const int y = itemY + static_cast<int>(i) * itemH;
    const bool active = i == selected;
    if (active) {
      display.fillRect(listX, y, listW, itemH - 3, true);
    }

    char idText[12];
    snprintf(idText, sizeof(idText), "#%d", model.messages[i].id);
    display.drawText(listX + 4, y + 4, idText, !active, 1);
    drawClippedText(display, listX + 38, y + 4, messageTitle(model.messages[i]), 12, !active, 1);
    drawClippedText(display, listX + 4, y + 17, model.messages[i].body, 18, !active, 1);
  }

  const HubMessage& message = model.messages[selected];
  drawClippedText(display, bodyX, topY + 12, formatMessageTimestamp(message.createdAt), 19, true, 1);
  drawWrappedText(display, bodyX, topY + 30, 19, 9, message.body, model.messageBodyScrollLine, true, 2, 21);

  display.drawText(24, 270, model.messageBodyFocused ? "KEY PAGE" : "KEY SELECT", true, 1);
  display.drawText(250, 270, "DBL FOCUS", true, 1);
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
    case DesktopClockPage::Message:
      drawMessagePage(display_, statusBar_, model);
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
  return static_cast<DesktopClockPage>((static_cast<int>(page) + 1) % 4);
}
