#pragma once

#include <Arduino.h>

#include "RlcdDisplay.h"

namespace Utf8Text {

constexpr int AsciiScale = 2;
constexpr int AsciiAdvance = 12;
constexpr int CjkAdvance = 16;
constexpr int LineHeight = 18;

void drawText(RlcdDisplay& display, int x, int y, const String& text, bool black = true);
void drawClipped(RlcdDisplay& display, int x, int y, int maxWidth, const String& text, bool black = true);
void drawWrapped(RlcdDisplay& display,
                 int x,
                 int y,
                 int maxWidth,
                 int maxLines,
                 const String& text,
                 uint16_t scrollLine = 0,
                 bool black = true,
                 int lineHeight = LineHeight);
uint16_t wrappedLineCount(const String& text, int maxWidth);

}  // namespace Utf8Text
