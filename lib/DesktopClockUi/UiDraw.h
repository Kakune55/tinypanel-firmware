#pragma once

#include "BatteryMonitor.h"
#include "PixelIcons.h"
#include "RlcdDisplay.h"

namespace UiDraw {

int clampInt(int value, int low, int high);
void bitmapScaled(RlcdDisplay& display, int x, int y, const PixelIcons::Bitmap& bitmap, int scale, bool black = true);
void batteryIcon(RlcdDisplay& display, int x, int y, const BatteryStatus& status, int scale = 2, bool black = true);
void progressBar(RlcdDisplay& display, int x, int y, int w, int h, int percent, bool black = true);

}  // namespace UiDraw
