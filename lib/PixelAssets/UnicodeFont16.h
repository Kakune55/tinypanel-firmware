#pragma once

#include <Arduino.h>
#include <cstdint>

namespace UnicodeFont16 {

constexpr uint8_t Width = 16;
constexpr uint8_t Height = 16;
constexpr uint8_t GlyphBytes = 32;
constexpr const char* DefaultPath = "/fonts/unicode16.uf16";

bool begin(const char* path = DefaultPath);
bool isReady();
const char* lastErrorText();
uint32_t glyphCount();
bool readGlyph(uint32_t codepoint, uint8_t out[GlyphBytes]);

}  // namespace UnicodeFont16
