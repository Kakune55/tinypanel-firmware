#include "DesktopClockUi.h"

#include <cstring>

#include "PixelFont5x7.h"
#include "StatusBar.h"
#include "UiDraw.h"
#include "Utf8Text.h"

namespace {

const PixelIcons::Bitmap& weatherIcon(const String& code);

const char* weekdayName(uint8_t weekday) {
  static constexpr const char* kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return weekday < 7 ? kNames[weekday] : "---";
}

const char* messageTitle(const HubMessage& message) {
  return message.author.length() > 0 ? message.author.c_str() : "anonymous";
}

const char* todoStatusLabel(int status) {
  switch (status) {
    case 1:
      return "DOING";
    case 2:
      return "DONE";
    default:
      return "TODO";
  }
}

const char* todoStatusGlyph(int status) {
  switch (status) {
    case 1:
      return ">";
    case 2:
      return "X";
    default:
      return " ";
  }
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
  for (int i = 0; i < kDesktopClockPageCount; ++i) {
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
    const uint8_t c = static_cast<uint8_t>(text[i]);
    if (c == '\n' || c == '\r') {
      buffer[out++] = ' ';
    } else if (c < 32 || c > 126) {
      buffer[out++] = '?';
    } else {
      buffer[out++] = static_cast<char>(c);
    }
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
  display.drawText(278, 194, "WEATHER", true, 1);
  const bool weatherOk = model.weather.valid;
  char tempText[16];
  char humText[16];
  if (weatherOk) {
    snprintf(tempText, sizeof(tempText), "%dC", model.weather.temperature);
    snprintf(humText, sizeof(humText), "%d%%", model.weather.humidity);
  } else {
    snprintf(tempText, sizeof(tempText), "--");
    snprintf(humText, sizeof(humText), "--");
  }

  const PixelIcons::Bitmap& icon = weatherOk ? weatherIcon(model.weather.icon) : PixelIcons::WeatherUnknown;
  UiDraw::bitmapScaled(display, 274, 212, icon, 1, true);
  display.drawText(300, 212, tempText, true, 2);
  display.drawText(300, 236, humText, true, 1);

  display.drawText(286, 270, "BOOT NEXT", true, 1);
  drawPageDots(display, model.page);
}

String formatWeatherTime(const String& value) {
  if (value.length() >= 16 && value[10] == 'T') {
    int hour = value.substring(11, 13).toInt();
    const int minute = value.substring(14, 16).toInt();
    if (value.endsWith("Z")) {
      constexpr int kLocalUtcOffsetHours = 8;
      hour = (hour + kLocalUtcOffsetHours) % 24;
    }

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return String(buffer);
  }
  return value;
}

String formatWeatherDate(const String& value) {
  if (value.length() >= 10) {
    return value.substring(5, 10);
  }
  return value;
}

void drawCenteredText(RlcdDisplay& display, int x, int y, int w, const char* text, bool black = true, int scale = 1) {
  const int textW = static_cast<int>(std::strlen(text)) * PixelFont5x7::Advance * scale;
  display.drawText(x + (w - textW) / 2, y, text, black, scale);
}

const PixelIcons::Bitmap& weatherIcon(const String& code) {
  const int value = code.toInt();
  if (value == 100 || value == 150) {
    return PixelIcons::WeatherSun;
  }
  if ((value >= 101 && value <= 103) || (value >= 151 && value <= 153)) {
    return PixelIcons::WeatherCloud;
  }
  if (value == 104 || value == 154) {
    return PixelIcons::WeatherOvercast;
  }
  if (value >= 300 && value < 400) {
    return PixelIcons::WeatherRain;
  }
  if (value >= 400 && value < 500) {
    return PixelIcons::WeatherSnow;
  }
  if (value >= 500 && value < 600) {
    return PixelIcons::WeatherFog;
  }
  return PixelIcons::WeatherUnknown;
}

void drawWeatherIconCentered(RlcdDisplay& display, int x, int y, int w, const String& code, int scale) {
  const PixelIcons::Bitmap& icon = weatherIcon(code);
  const int iconW = icon.width * scale;
  UiDraw::bitmapScaled(display, x + (w - iconW) / 2, y, icon, scale, true);
}

void drawWeatherPage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  char text[56];

  display.clear(true);
  statusBar.draw(model);

  if (!model.weather.valid) {
    display.drawRect(12, 44, 376, 214, true);
    display.drawText(34, 112, model.wifiConnected ? "Weather pending" : "Weather offline", true, 2);
    display.drawText(286, 270, "BOOT NEXT", true, 1);
    drawPageDots(display, model.page);
    return;
  }

  constexpr int gridX = 8;
  constexpr int topY = 36;
  constexpr int gridW = 384;
  constexpr int currentW = 188;
  constexpr int topH = 100;
  constexpr int hourlyY = topY + topH;
  constexpr int hourlyH = 96;
  constexpr int detailY = hourlyY + hourlyH;
  constexpr int detailH = 30;

  display.drawRect(gridX, topY, gridW, topH + hourlyH + detailH, true);
  display.drawFastHLine(gridX, hourlyY, gridW, true);
  display.drawFastHLine(gridX, detailY, gridW, true);
  display.drawFastVLine(gridX + currentW, topY, topH, true);

  snprintf(text, sizeof(text), "%dC", model.weather.temperature);
  display.drawText(22, 58, text, true, 5);

  if (model.weather.dailyCount > 0) {
    const HubWeatherDaily& today = model.weather.daily[0];
    display.drawText(120, 56, "MAX", true, 1);
    snprintf(text, sizeof(text), "%dC", today.temperatureMax);
    display.drawText(148, 52, text, true, 2);
    display.drawText(120, 86, "MIN", true, 1);
    snprintf(text, sizeof(text), "%dC", today.temperatureMin);
    display.drawText(148, 82, text, true, 2);
  } else {
    snprintf(text, sizeof(text), "HUM %d%%", model.weather.humidity);
    display.drawText(108, 58, text, true, 2);
  }

  drawWeatherIconCentered(display, 20, 104, 42, model.weather.icon, 2);
  snprintf(text, sizeof(text), "UPD %s", formatWeatherTime(model.weather.updatedAt).c_str());
  display.drawText(72, 114, text, true, 1);

  constexpr int forecastCols = 3;
  const int forecastW = (gridW - currentW) / forecastCols;
  for (int i = 0; i < forecastCols; ++i) {
    const int x = gridX + currentW + i * forecastW;
    display.drawFastVLine(x, topY, topH, true);
    snprintf(text, sizeof(text), "+%d", i + 1);
    drawCenteredText(display, x, topY + 14, forecastW, text, true, 2);

    const size_t dailyIndex = static_cast<size_t>(i + 1);
    if (dailyIndex < model.weather.dailyCount) {
      const HubWeatherDaily& day = model.weather.daily[dailyIndex];
      snprintf(text, sizeof(text), "%d/%dC", day.temperatureMax, day.temperatureMin);
      drawCenteredText(display, x, topY + 48, forecastW, text, true, 1);
      drawWeatherIconCentered(display, x, topY + 70, forecastW, day.iconDay, 1);
    } else {
      drawCenteredText(display, x, topY + 54, forecastW, "--", true, 2);
    }
  }

  constexpr int hourlyCols = 8;
  constexpr int hourlyRows = 2;
  constexpr int hourlyCellW = gridW / hourlyCols;
  constexpr int hourlyCellH = hourlyH / hourlyRows;
  for (int i = 1; i < hourlyCols; ++i) {
    display.drawFastVLine(gridX + i * hourlyCellW, hourlyY, hourlyH, true);
  }
  display.drawFastHLine(gridX, hourlyY + hourlyCellH, gridW, true);

  const size_t hourlyLimit = min(model.weather.hourlyCount, static_cast<size_t>(hourlyCols * hourlyRows));
  for (size_t i = 0; i < hourlyLimit; ++i) {
    const HubWeatherHourly& item = model.weather.hourly[i];
    const int col = static_cast<int>(i % hourlyCols);
    const int row = static_cast<int>(i / hourlyCols);
    const int x = gridX + col * hourlyCellW;
    const int y = hourlyY + row * hourlyCellH;
    snprintf(text, sizeof(text), "%s", formatWeatherTime(item.time).c_str());
    drawCenteredText(display, x, y + 5, hourlyCellW, text, true, 1);
    snprintf(text, sizeof(text), "%dC", item.temperature);
    drawCenteredText(display, x, y + 21, hourlyCellW, text, true, 1);
    drawWeatherIconCentered(display, x, y + 32, hourlyCellW, item.icon, 1);
  }

  snprintf(text, sizeof(text), "HUM %d%%", model.weather.humidity);
  display.drawText(22, detailY + 12, text, true, 1);
  if (model.weather.dailyCount > 0) {
    const HubWeatherDaily& today = model.weather.daily[0];
    snprintf(text, sizeof(text), "TODAY %s", formatWeatherDate(today.date).c_str());
    display.drawText(102, detailY + 12, text, true, 1);
    snprintf(text, sizeof(text), "SUN %s-%s", today.sunrise.c_str(), today.sunset.c_str());
    display.drawText(204, detailY + 12, text, true, 1);
  } else {
    display.drawText(102, detailY + 12, "TODAY --", true, 1);
  }

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
      display.fillRect(listX, y, listW, itemH + 1, true); 
    }

    char idText[12];
    snprintf(idText, sizeof(idText), "#%d", model.messages[i].id);
    display.drawText(listX + 4, y + 4, idText, !active, 1);
    drawClippedText(display, listX + 38, y + 4, messageTitle(model.messages[i]), 12, !active, 1);
    Utf8Text::drawClipped(display, listX + 4, y + 13, listW - 8, model.messages[i].body, !active);
  }

  const HubMessage& message = model.messages[selected];
  drawClippedText(display, bodyX, topY + 12, formatMessageTimestamp(message.createdAt), 19, true, 1);
  Utf8Text::drawWrapped(display, bodyX, topY + 30, bodyW, 11, message.body, model.messageBodyScrollLine, true, 18);

  display.drawText(24, 270, model.messageBodyFocused ? "KEY PAGE" : "KEY SELECT", true, 1);
  display.drawText(250, 270, "DBL FOCUS", true, 1);
  drawPageDots(display, model.page);
}

void drawTodoPage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  display.clear(true);
  statusBar.draw(model);

  constexpr int x = 6;
  constexpr int y = 38;
  constexpr int w = 388;
  constexpr int itemH = 30;

  if (!model.todos || model.todoCount == 0) {
    display.drawText(x + 18, y + 78, model.wifiConnected ? "No TODO items" : "TODO offline", true, 2);
    display.drawText(24, 270, "KEY SELECT", true, 1);
    display.drawText(132, 270, "DBL STATUS", true, 1);
    display.drawText(270, 270, "HOLD DEL", true, 1);
    drawPageDots(display, model.page);
    return;
  }

  const size_t selected = min(model.selectedTodo, model.todoCount - 1);
  const size_t visible = min(model.todoCount, static_cast<size_t>(7));
  size_t start = 0;
  if (selected >= visible) {
    start = selected - visible + 1;
  }

  for (size_t row = 0; row < visible && start + row < model.todoCount; ++row) {
    const size_t index = start + row;
    const HubTodo& todo = model.todos[index];
    const int itemY = y + static_cast<int>(row) * itemH;
    const bool active = index == selected;
    if (active) {
      display.fillRect(x, itemY - 2, w, itemH - 2, true);
    }

    display.drawRect(x + 6, itemY + 6, 12, 12, !active);
    display.drawText(x + 9, itemY + 9, todoStatusGlyph(todo.status), !active, 1);
    Utf8Text::drawClipped(display, x + 28, itemY + 2, w - 34, todo.text, !active);
  }

  display.drawText(24, 270, "KEY SELECT", true, 1);
  display.drawText(132, 270, "DBL STATUS", true, 1);
  display.drawText(270, 270, "HOLD DEL", true, 1);
  drawPageDots(display, model.page);
}

void drawSystemPage(RlcdDisplay& display, StatusBar& statusBar, const DesktopClockUiModel& model) {
  char text[48];

  display.clear(true);
  statusBar.draw(model);

  constexpr int leftX = 16;
  constexpr int rightX = 212;
  constexpr int topY = 42;

  display.drawText(leftX, topY, "BATTERY", true, 2);
  snprintf(text, sizeof(text), "%.2f%%", model.battery.percentFloat);
  display.drawText(leftX, topY + 28, text, true, 3);

  snprintf(text, sizeof(text), "%.3fV ADC %d", model.battery.voltage, model.battery.rawAdc);
  display.drawText(leftX, topY + 62, text, true, 1);

  snprintf(text, sizeof(text), "RAW %lumV %s",
           static_cast<unsigned long>(model.battery.rawVoltageMv),
           model.battery.charging ? "CHG" : "BAT");
  display.drawText(leftX, topY + 80, text, true, 1);

  if (model.batteryEtaMinutes >= 0) {
    snprintf(text, sizeof(text), "ETA %dh%02dm", model.batteryEtaMinutes / 60, model.batteryEtaMinutes % 60);
  } else {
    snprintf(text, sizeof(text), "ETA --");
  }
  display.drawText(leftX, topY + 98, text, true, 2);

  if (model.environment.valid) {
    snprintf(text, sizeof(text), "ENV %.1fC %.0f%%", model.environment.temperatureC, model.environment.humidityRh);
  } else {
    snprintf(text, sizeof(text), "ENV --");
  }
  display.drawText(leftX, topY + 128, text, true, 1);

  display.drawText(rightX, topY, "NETWORK", true, 2);
  snprintf(text, sizeof(text), "WIFI %s %d", model.wifiConnected ? "ON" : "OFF", model.wifiRssi);
  display.drawText(rightX, topY + 28, text, true, 1);

  drawClippedText(display, rightX, topY + 46, model.wifiConnected ? model.wifiSsid : String("--"), 22, true, 1);

  snprintf(text, sizeof(text), "IP %s", model.wifiConnected ? model.wifiIp.c_str() : "--");
  display.drawText(rightX, topY + 64, text, true, 1);

  snprintf(text, sizeof(text), "NTP %s", model.ntpSynced ? "OK" : "WAIT");
  display.drawText(rightX, topY + 82, text, true, 1);

  snprintf(text, sizeof(text), "HUB %s", model.hubSyncing ? "SYNC" : (model.hubSyncFailed ? "FAIL" : "OK"));
  display.drawText(rightX, topY + 100, text, true, 1);

  snprintf(text, sizeof(text), "MSG %u TODO %u", static_cast<unsigned>(model.messageCount),
           static_cast<unsigned>(model.todoCount));
  display.drawText(rightX, topY + 118, text, true, 1);

  snprintf(text, sizeof(text), "HEAP %luK", static_cast<unsigned long>(model.freeHeap / 1024UL));
  display.drawText(rightX, topY + 146, text, true, 1);

  snprintf(text, sizeof(text), "PSRAM %luK", static_cast<unsigned long>(model.freePsram / 1024UL));
  display.drawText(rightX, topY + 164, text, true, 1);

  snprintf(text, sizeof(text), "SD %s", model.sdMounted ? "READY" : model.sdStatus);
  display.drawText(rightX, topY + 182, text, true, 1);

  snprintf(text, sizeof(text), "UP %luh%02lum", static_cast<unsigned long>(model.uptimeMs / 3600000UL),
           static_cast<unsigned long>((model.uptimeMs / 60000UL) % 60UL));
  display.drawText(leftX, topY + 164, text, true, 1);

  display.drawText(24, 270, "KEY REFRESH", true, 1);
  display.drawText(286, 270, "BOOT NEXT", true, 1);
  drawPageDots(display, model.page);
}

void drawNewMessageModal(RlcdDisplay& display, bool invert) {
  constexpr int x = 92;
  constexpr int y = 94;
  constexpr int w = 216;
  constexpr int h = 108;
  const bool bg = invert;
  const bool ink = !invert;

  display.fillRect(x, y, w, h, bg);
  display.drawRect(x, y, w, h, ink);
  display.drawRect(x + 2, y + 2, w - 4, h - 4, ink);
  display.drawText(x + 22, y + 20, "New Msg!", ink, 4);
  display.drawText(x + 20, y + 68, "KEY OPEN", ink, 2);
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
    case DesktopClockPage::Todo:
      drawTodoPage(display_, statusBar_, model);
      break;
    case DesktopClockPage::Weather:
      drawWeatherPage(display_, statusBar_, model);
      break;
    case DesktopClockPage::System:
      drawSystemPage(display_, statusBar_, model);
      break;
  }
  if (model.newMessageAlert) {
    drawNewMessageModal(display_, model.newMessageAlertInvert);
  }
  display_.flushFull();
}

DesktopClockPage DesktopClockUi::nextPage(DesktopClockPage page) {
  return static_cast<DesktopClockPage>((static_cast<int>(page) + 1) % kDesktopClockPageCount);
}
