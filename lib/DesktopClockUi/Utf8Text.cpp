#include "Utf8Text.h"

#include <cstring>

#include "GB2312Map.h"
#include "HZK16.h"
#include "PixelFont5x7.h"

namespace {

constexpr uint32_t kReplacement = '?';
constexpr const char* kBitmapPrefix = "[[BMP:";
constexpr const char* kBitmapSuffix = "]]";

struct InlineBitmap {
  size_t endIndex = 0;
  size_t hexStart = 0;
  size_t hexEnd = 0;
  uint8_t width = 0;
  uint8_t height = 0;
  bool hasWhitespace = false;
};

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

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool isInlineBitmapWhitespace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

bool hexByteAt(const String& text, size_t hexStart, size_t hexEnd, bool hasWhitespace, size_t byteIndex, uint8_t& out) {
  if (hasWhitespace) {
    int nibbles[2] = {-1, -1};
    size_t targetNibble = byteIndex * 2;
    size_t seen = 0;
    for (size_t i = hexStart; i < hexEnd; ++i) {
      const char c = text[i];
      if (isInlineBitmapWhitespace(c)) {
        continue;
      }
      if (seen == targetNibble) {
        nibbles[0] = hexValue(c);
      } else if (seen == targetNibble + 1) {
        nibbles[1] = hexValue(c);
        break;
      }
      ++seen;
    }
    if (nibbles[0] < 0 || nibbles[1] < 0) {
      return false;
    }
    out = static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]);
    return true;
  }

  const size_t index = hexStart + byteIndex * 2;
  if (index + 1 >= hexEnd) {
    return false;
  }
  const int high = hexValue(text[index]);
  const int low = hexValue(text[index + 1]);
  if (high < 0 || low < 0) {
    return false;
  }
  out = static_cast<uint8_t>((high << 4) | low);
  return true;
}

bool parseInlineBitmapAt(const String& text, size_t index, InlineBitmap& bitmap) {
  const size_t prefixLen = std::strlen(kBitmapPrefix);
  const size_t suffixLen = std::strlen(kBitmapSuffix);
  if (index + prefixLen >= text.length()) {
    return false;
  }
  for (size_t i = 0; i < prefixLen; ++i) {
    if (text[index + i] != kBitmapPrefix[i]) {
      return false;
    }
  }

  const int suffix = text.indexOf(kBitmapSuffix, index + prefixLen);
  if (suffix < 0) {
    return false;
  }

  size_t compactHexLen = 0;
  for (int i = static_cast<int>(index + prefixLen); i < suffix; ++i) {
    const char c = text[i];
    if (isInlineBitmapWhitespace(c)) {
      bitmap.hasWhitespace = true;
      continue;
    }
    if (hexValue(c) < 0) {
      return false;
    }
    ++compactHexLen;
  }
  if (compactHexLen < 6 || (compactHexLen & 1) != 0) {
    return false;
  }

  uint8_t header = 0;
  const size_t hexStart = index + prefixLen;
  const size_t hexEnd = static_cast<size_t>(suffix);
  if (!hexByteAt(text, hexStart, hexEnd, bitmap.hasWhitespace, 0, header)) {
    return false;
  }
  if (header != 0xFF) {
    return false;
  }
  if (!hexByteAt(text, hexStart, hexEnd, bitmap.hasWhitespace, 1, bitmap.width) ||
      !hexByteAt(text, hexStart, hexEnd, bitmap.hasWhitespace, 2, bitmap.height)) {
    return false;
  }
  if (bitmap.width == 0 || bitmap.height == 0) {
    return false;
  }
  const size_t pixelCount = static_cast<size_t>(bitmap.width) * bitmap.height;
  const size_t requiredBytes = 3 + (pixelCount + 7) / 8;
  if (compactHexLen / 2 < requiredBytes) {
    return false;
  }

  bitmap.endIndex = static_cast<size_t>(suffix) + suffixLen;
  bitmap.hexStart = hexStart;
  bitmap.hexEnd = hexEnd;
  return true;
}

void drawInlineBitmap(RlcdDisplay& display, int x, int y, const String& text, const InlineBitmap& bitmap, bool black) {
  size_t byteIndex = 3;
  uint8_t byte = 0;
  uint8_t bitsLeft = 0;
  for (uint8_t row = 0; row < bitmap.height; ++row) {
    for (uint8_t col = 0; col < bitmap.width; ++col) {
      if (bitsLeft == 0) {
        if (!hexByteAt(text, bitmap.hexStart, bitmap.hexEnd, bitmap.hasWhitespace, byteIndex++, byte)) {
          return;
        }
        bitsLeft = 8;
      }
      if ((byte & 0x80) != 0) {
        display.setPixel(x + col, y + row, black);
      }
      byte <<= 1;
      --bitsLeft;
    }
  }
}

uint16_t inlineBitmapLineSpan(const InlineBitmap& bitmap, int lineHeight) {
  return static_cast<uint16_t>(max(1, (bitmap.height + lineHeight - 1) / lineHeight));
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
    InlineBitmap bitmap;
    if (parseInlineBitmapAt(text, index, bitmap)) {
      if (cursorX > x) {
        ++logicalLine;
        cursorX = x;
      }
      const uint16_t lineSpan = inlineBitmapLineSpan(bitmap, lineHeight);
      if (logicalLine + lineSpan > scrollLine) {
        const int drawLine = static_cast<int>(logicalLine - scrollLine);
        if (drawLine >= maxLines) {
          return;
        }
        const int drawY = y + drawLine * lineHeight;
        if (drawY + bitmap.height > y) {
          drawInlineBitmap(display,
                           x + max(0, (maxWidth - static_cast<int>(bitmap.width)) / 2),
                           drawY,
                           text,
                           bitmap,
                           black);
        }
      }
      cursorX = x;
      logicalLine += lineSpan;
      index = bitmap.endIndex;
      continue;
    }

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
    InlineBitmap bitmap;
    if (parseInlineBitmapAt(text, index, bitmap)) {
      if (cursor > 0) {
        ++lines;
        cursor = 0;
      }
      const uint16_t lineSpan = inlineBitmapLineSpan(bitmap, LineHeight);
      lines += lineSpan;
      cursor = 0;
      index = bitmap.endIndex;
      continue;
    }

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
