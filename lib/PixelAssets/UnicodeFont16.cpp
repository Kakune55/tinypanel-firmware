#include "UnicodeFont16.h"

#include <FS.h>
#include <LittleFS.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr uint8_t kVersion = 1;
constexpr uint8_t kHeaderSize = 24;
constexpr char kMagic[6] = {'T', 'P', 'U', '1', '6', '\0'};
constexpr uint8_t kCacheSlots = 32;

File fontFile;
bool ready = false;
const char* lastError = "not started";
uint32_t count = 0;
uint32_t indexOffset = 0;
uint32_t bitmapOffset = 0;
uint32_t* codepointIndex = nullptr;

struct GlyphCacheEntry {
  uint32_t codepoint = 0;
  bool valid = false;
  bool found = false;
  uint8_t bitmap[UnicodeFont16::GlyphBytes] = {};
};

GlyphCacheEntry glyphCache[kCacheSlots];
uint8_t nextCacheSlot = 0;

uint32_t readLe32At(uint32_t offset) {
  uint8_t bytes[4] = {0, 0, 0, 0};
  if (!fontFile.seek(offset) || fontFile.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    return 0;
  }
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

bool validateHeader() {
  uint8_t header[kHeaderSize] = {};
  if (!fontFile.seek(0) || fontFile.read(header, sizeof(header)) != sizeof(header)) {
    lastError = "short header";
    return false;
  }
  for (uint8_t i = 0; i < sizeof(kMagic); ++i) {
    if (header[i] != static_cast<uint8_t>(kMagic[i])) {
      lastError = "bad magic";
      return false;
    }
  }
  if (header[6] != kVersion || header[7] != UnicodeFont16::Width ||
      header[8] != UnicodeFont16::Height || header[9] != UnicodeFont16::GlyphBytes) {
    lastError = "unsupported format";
    return false;
  }

  count = static_cast<uint32_t>(header[12]) |
          (static_cast<uint32_t>(header[13]) << 8) |
          (static_cast<uint32_t>(header[14]) << 16) |
          (static_cast<uint32_t>(header[15]) << 24);
  indexOffset = static_cast<uint32_t>(header[16]) |
                (static_cast<uint32_t>(header[17]) << 8) |
                (static_cast<uint32_t>(header[18]) << 16) |
                (static_cast<uint32_t>(header[19]) << 24);
  bitmapOffset = static_cast<uint32_t>(header[20]) |
                 (static_cast<uint32_t>(header[21]) << 8) |
                 (static_cast<uint32_t>(header[22]) << 16) |
                 (static_cast<uint32_t>(header[23]) << 24);

  const uint32_t expectedSize = bitmapOffset + count * UnicodeFont16::GlyphBytes;
  if (count == 0 || indexOffset < kHeaderSize || bitmapOffset < indexOffset + count * 4) {
    lastError = "bad offsets";
    return false;
  }
  if (fontFile.size() < expectedSize) {
    lastError = "truncated file";
    return false;
  }
  return true;
}

uint32_t* allocIndex(size_t bytes) {
  uint32_t* buffer = static_cast<uint32_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) {
    buffer = static_cast<uint32_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  }
  return buffer;
}

void resetCache() {
  for (auto& entry : glyphCache) {
    entry.valid = false;
    entry.found = false;
    entry.codepoint = 0;
  }
  nextCacheSlot = 0;
}

void putCache(uint32_t codepoint, const uint8_t* bitmap) {
  GlyphCacheEntry& entry = glyphCache[nextCacheSlot];
  nextCacheSlot = static_cast<uint8_t>((nextCacheSlot + 1) % kCacheSlots);
  entry.codepoint = codepoint;
  entry.valid = true;
  entry.found = bitmap != nullptr;
  if (bitmap) {
    std::memcpy(entry.bitmap, bitmap, UnicodeFont16::GlyphBytes);
  }
}

bool loadIndex() {
  const size_t bytes = static_cast<size_t>(count) * sizeof(uint32_t);
  codepointIndex = allocIndex(bytes);
  if (!codepointIndex) {
    lastError = "index alloc failed";
    return false;
  }

  if (!fontFile.seek(indexOffset) || fontFile.read(reinterpret_cast<uint8_t*>(codepointIndex), bytes) != bytes) {
    heap_caps_free(codepointIndex);
    codepointIndex = nullptr;
    lastError = "index read failed";
    return false;
  }
  return true;
}

}  // namespace

namespace UnicodeFont16 {

bool begin(const char* path) {
  ready = false;
  count = 0;
  indexOffset = 0;
  bitmapOffset = 0;
  resetCache();
  if (codepointIndex) {
    heap_caps_free(codepointIndex);
    codepointIndex = nullptr;
  }
  if (fontFile) {
    fontFile.close();
  }

  if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
    lastError = "LittleFS mount failed";
    return false;
  }

  if (!LittleFS.exists(path)) {
    lastError = "font file missing";
    return false;
  }

  fontFile = LittleFS.open(path, "r");
  if (!fontFile) {
    lastError = "font open failed";
    return false;
  }

  ready = validateHeader();
  if (ready) {
    ready = loadIndex();
  }
  if (!ready) {
    fontFile.close();
  } else {
    lastError = "ok";
  }
  return ready;
}

bool isReady() {
  return ready && fontFile;
}

const char* lastErrorText() {
  return lastError;
}

uint32_t glyphCount() {
  return ready ? count : 0;
}

bool readGlyph(uint32_t codepoint, uint8_t out[GlyphBytes]) {
  if (!isReady() || out == nullptr) {
    return false;
  }

  for (const auto& entry : glyphCache) {
    if (entry.valid && entry.codepoint == codepoint) {
      if (entry.found) {
        std::memcpy(out, entry.bitmap, GlyphBytes);
      }
      return entry.found;
    }
  }

  uint32_t low = 0;
  uint32_t high = count;
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2;
    const uint32_t value = codepointIndex[mid];
    if (value == codepoint) {
      const uint32_t offset = bitmapOffset + mid * GlyphBytes;
      const bool ok = fontFile.seek(offset) && fontFile.read(out, GlyphBytes) == GlyphBytes;
      putCache(codepoint, ok ? out : nullptr);
      return ok;
    }
    if (value < codepoint) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  putCache(codepoint, nullptr);
  return false;
}

}  // namespace UnicodeFont16
