#include "Utf8Text.h"

#include "GB2312Map.h"
#include "HZK16.h"
#include "PixelFont5x7.h"

namespace {

constexpr uint32_t kReplacement = '?';

uint32_t nextCodepoint(const String& text, size_t& index) {
  if (index >= text.length()) {
    return 0;
  }

  const uint8_t first = static_cast<uint8_t>(text[index++]);
  if (first < 0x80) {
    return first;
  }

  uint32_t codepoint = 0;
  uint8_t needed = 0;
  if ((first & 0xE0) == 0xC0) {
    codepoint = first & 0x1F;
    needed = 1;
  } else if ((first & 0xF0) == 0xE0) {
    codepoint = first & 0x0F;
    needed = 2;
  } else if ((first & 0xF8) == 0xF0) {
    codepoint = first & 0x07;
    needed = 3;
  } else {
    return kReplacement;
  }

  for (uint8_t i = 0; i < needed; ++i) {
    if (index >= text.length()) {
      return kReplacement;
    }
    const uint8_t next = static_cast<uint8_t>(text[index++]);
    if ((next & 0xC0) != 0x80) {
      return kReplacement;
    }
    codepoint = (codepoint << 6) | (next & 0x3F);
  }
  return codepoint;
}

int codepointWidth(uint32_t codepoint) {
  if (codepoint == '\n' || codepoint == '\r') {
    return 0;
  }
  return codepoint < 0x80 ? Utf8Text::AsciiAdvance : Utf8Text::CjkAdvance;
}

void drawAscii(RlcdDisplay& display, int x, int y, char c, bool black) {
  const uint8_t* rows = PixelFont5x7::glyphRows(c);
  for (int row = 0; row < PixelFont5x7::Height; ++row) {
    for (int col = 0; col < PixelFont5x7::Width; ++col) {
      if ((rows[row] & (0x10 >> col)) == 0) {
        continue;
      }
      display.fillRect(x + col * Utf8Text::AsciiScale, y + 1 + row * Utf8Text::AsciiScale,
                       Utf8Text::AsciiScale, Utf8Text::AsciiScale, black);
    }
  }
}

void drawMissing(RlcdDisplay& display, int x, int y, bool black) {
  display.drawRect(x + 1, y + 1, 14, 14, black);
  display.drawLine(x + 3, y + 3, x + 12, y + 12, black);
  display.drawLine(x + 12, y + 3, x + 3, y + 12, black);
}

void drawCjk(RlcdDisplay& display, int x, int y, uint32_t codepoint, bool black) {
  const uint16_t gb2312 = GB2312Map::toGb2312(codepoint);
  if (gb2312 == 0) {
    drawMissing(display, x, y, black);
    return;
  }

  const uint8_t area = static_cast<uint8_t>(gb2312 >> 8);
  const uint8_t index = static_cast<uint8_t>(gb2312 & 0xFF);
  if (area < 0xA1 || area > 0xFE || index < 0xA1 || index > 0xFE) {
    drawMissing(display, x, y, black);
    return;
  }

  const uint32_t offset = ((area - 0xA1) * 94UL + (index - 0xA1)) * 32UL;
  if (offset + 32 > HZK16::Size) {
    drawMissing(display, x, y, black);
    return;
  }

  const uint8_t* bitmap = HZK16::Data + offset;

  for (int row = 0; row < 16; ++row) {
    for (int col = 0; col < 16; ++col) {
      const uint8_t byte = bitmap[row * 2 + (col >> 3)];
      if ((byte & (0x80 >> (col & 0x07))) == 0) {
        continue;
      }
      display.setPixel(x + col, y + row, black);
    }
  }
}

void drawCodepoint(RlcdDisplay& display, int x, int y, uint32_t codepoint, bool black) {
  if (codepoint < 0x80) {
    drawAscii(display, x, y, static_cast<char>(codepoint), black);
    return;
  }
  drawCjk(display, x, y, codepoint, black);
}

}  // namespace

namespace Utf8Text {

void drawText(RlcdDisplay& display, int x, int y, const String& text, bool black) {
  int cursorX = x;
  int cursorY = y;
  for (size_t index = 0; index < text.length();) {
    const uint32_t codepoint = nextCodepoint(text, index);
    if (codepoint == '\r') {
      continue;
    }
    if (codepoint == '\n') {
      cursorX = x;
      cursorY += LineHeight;
      continue;
    }
    drawCodepoint(display, cursorX, cursorY, codepoint, black);
    cursorX += codepointWidth(codepoint);
  }
}

void drawClipped(RlcdDisplay& display, int x, int y, int maxWidth, const String& text, bool black) {
  int cursorX = x;
  const int right = x + maxWidth;
  for (size_t index = 0; index < text.length();) {
    const uint32_t codepoint = nextCodepoint(text, index);
    if (codepoint == '\r') {
      continue;
    }
    if (codepoint == '\n') {
      break;
    }
    const int width = codepointWidth(codepoint);
    if (cursorX + width > right) {
      break;
    }
    drawCodepoint(display, cursorX, y, codepoint, black);
    cursorX += width;
  }
}

void drawWrapped(RlcdDisplay& display,
                 int x,
                 int y,
                 int maxWidth,
                 int maxLines,
                 const String& text,
                 uint16_t scrollLine,
                 bool black,
                 int lineHeight) {
  if (maxWidth <= 0 || maxLines <= 0) {
    return;
  }

  int cursorX = x;
  uint16_t logicalLine = 0;
  for (size_t index = 0; index < text.length();) {
    const uint32_t codepoint = nextCodepoint(text, index);
    if (codepoint == '\r') {
      continue;
    }
    if (codepoint == '\n') {
      ++logicalLine;
      cursorX = x;
      continue;
    }

    const int width = codepointWidth(codepoint);
    if (cursorX > x && cursorX + width > x + maxWidth) {
      ++logicalLine;
      cursorX = x;
    }
    if (logicalLine >= scrollLine) {
      const int drawLine = static_cast<int>(logicalLine - scrollLine);
      if (drawLine >= maxLines) {
        return;
      }
      drawCodepoint(display, cursorX, y + drawLine * lineHeight, codepoint, black);
    }
    cursorX += width;
  }
}

uint16_t wrappedLineCount(const String& text, int maxWidth) {
  if (maxWidth <= 0) {
    return 1;
  }

  uint16_t lines = 1;
  int cursor = 0;
  for (size_t index = 0; index < text.length();) {
    const uint32_t codepoint = nextCodepoint(text, index);
    if (codepoint == '\r') {
      continue;
    }
    if (codepoint == '\n') {
      ++lines;
      cursor = 0;
      continue;
    }
    const int width = codepointWidth(codepoint);
    if (cursor > 0 && cursor + width > maxWidth) {
      ++lines;
      cursor = 0;
    }
    cursor += width;
  }
  return lines;
}

}  // namespace Utf8Text
