#include "UiDraw.h"

namespace {

constexpr int kBatteryIconCells = 21;

int batteryFilledCells(int percent) {
  percent = UiDraw::clampInt(percent, 0, 100);
  if (percent == 0) {
    return 0;
  }
  return UiDraw::clampInt((percent * kBatteryIconCells + 99) / 100, 0, kBatteryIconCells);
}

}  // namespace

namespace UiDraw {

int clampInt(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

void bitmapScaled(RlcdDisplay& display, int x, int y, const PixelIcons::Bitmap& bitmap, int scale, bool black) {
  if (!bitmap.data || bitmap.width <= 0 || bitmap.height <= 0) {
    return;
  }

  scale = max(1, scale);
  const int stride = (bitmap.width + 7) / 8;
  for (int yy = 0; yy < bitmap.height; ++yy) {
    for (int xx = 0; xx < bitmap.width; ++xx) {
      const uint8_t byte = bitmap.data[yy * stride + (xx >> 3)];
      const bool bitSet = (byte & (0x80 >> (xx & 0x07))) != 0;
      if (!bitSet) {
        continue;
      }

      if (scale == 1) {
        display.setPixel(x + xx, y + yy, black);
      } else {
        display.fillRect(x + xx * scale, y + yy * scale, scale, scale, black);
      }
    }
  }
}

void batteryIcon(RlcdDisplay& display, int x, int y, const BatteryStatus& status, int scale, bool black) {
  scale = max(1, scale);

  if (status.voltage < 0.5f) {
    bitmapScaled(display, x, y, PixelIcons::BatteryOffline, scale, black);
    return;
  }

  if (status.critical) {
    bitmapScaled(display, x, y, PixelIcons::BatteryError, scale, black);
    return;
  }

  bitmapScaled(display, x, y, PixelIcons::BatteryTemplate, scale, black);

  const int filled = batteryFilledCells(status.percent);
  for (int i = 0; i < filled; ++i) {
    const int cellRow = i % 3;
    const int cellCol = i / 3;
    const int px = x + (2 + cellCol) * scale;
    const int py = y + (2 + cellRow) * scale;
    display.fillRect(px, py, scale, scale, black);
  }
}

void progressBar(RlcdDisplay& display, int x, int y, int w, int h, int percent, bool black) {
  display.drawRect(x, y, w, h, black);
  int inner = clampInt((w - 4) * percent / 100, 0, w - 4);
  if (inner > 0) {
    display.fillRect(x + 2, y + 2, inner, h - 4, black);
  }
}

}  // namespace UiDraw
