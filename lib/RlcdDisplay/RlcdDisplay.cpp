#include "RlcdDisplay.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

#include "BoardConfig.h"
#include "PixelFont5x7.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"

namespace {

constexpr int kSpiClockHz = 10 * 1000 * 1000;
constexpr uint8_t kWhiteByte = 0xFF;
constexpr uint8_t kBlackByte = 0x00;
constexpr spi_host_device_t kSpiHost = SPI3_HOST;

esp_lcd_panel_io_handle_t g_ioHandle = nullptr;

inline uint32_t pixelIndex(int x, int y, uint8_t& mask) {
  const uint16_t invY = BoardConfig::RlcdHeight - 1 - y;
  const uint16_t byteX = x >> 1;
  const uint16_t blockY = invY >> 2;
  const uint8_t localX = x & 0x01;
  const uint8_t localY = invY & 0x03;
  mask = 1U << (7 - ((localY << 1) | localX));
  return byteX * (BoardConfig::RlcdHeight >> 2) + blockY;
}

inline void writePixelUnchecked(uint8_t* buffer, int x, int y, bool black) {
  uint8_t mask = 0;
  const uint32_t index = pixelIndex(x, y, mask);
  if (black) {
    buffer[index] &= ~mask;
  } else {
    buffer[index] |= mask;
  }
}

}  // namespace

bool RlcdDisplay::begin() {
  if (ready_) {
    return true;
  }

  bufferLen_ = (BoardConfig::RlcdWidth * BoardConfig::RlcdHeight) / 8;
  buffer_ = static_cast<uint8_t*>(heap_caps_malloc(bufferLen_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer_ == nullptr) {
    buffer_ = static_cast<uint8_t*>(std::malloc(bufferLen_));
  }

  if (buffer_ == nullptr) {
    Serial.println("RlcdDisplay: framebuffer allocation failed");
    return false;
  }

  std::memset(buffer_, kWhiteByte, bufferLen_);

  spi_bus_config_t busConfig = {};
  busConfig.miso_io_num = -1;
  busConfig.mosi_io_num = BoardConfig::RlcdMosi;
  busConfig.sclk_io_num = BoardConfig::RlcdSclk;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = BoardConfig::RlcdWidth * BoardConfig::RlcdHeight;

  esp_err_t err = spi_bus_initialize(kSpiHost, &busConfig, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("RlcdDisplay: spi_bus_initialize failed: 0x%X\n", err);
    releaseBuffer();
    return false;
  }

  esp_lcd_panel_io_spi_config_t ioConfig = {};
  ioConfig.dc_gpio_num = BoardConfig::RlcdDc;
  ioConfig.cs_gpio_num = BoardConfig::RlcdCs;
  ioConfig.pclk_hz = kSpiClockHz;
  ioConfig.lcd_cmd_bits = 8;
  ioConfig.lcd_param_bits = 8;
  ioConfig.spi_mode = 0;
  ioConfig.trans_queue_depth = 10;

  err = esp_lcd_new_panel_io_spi(reinterpret_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &ioConfig, &g_ioHandle);
  if (err != ESP_OK) {
    Serial.printf("RlcdDisplay: esp_lcd_new_panel_io_spi failed: 0x%X\n", err);
    releaseBuffer();
    return false;
  }

  gpio_config_t resetConfig = {};
  resetConfig.intr_type = GPIO_INTR_DISABLE;
  resetConfig.mode = GPIO_MODE_OUTPUT;
  resetConfig.pin_bit_mask = 1ULL << BoardConfig::RlcdRst;
  resetConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  resetConfig.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&resetConfig);

  ready_ = true;
  initPanel();
  return true;
}

bool RlcdDisplay::isReady() const {
  return ready_;
}

int RlcdDisplay::width() const {
  return BoardConfig::RlcdWidth;
}

int RlcdDisplay::height() const {
  return BoardConfig::RlcdHeight;
}

void RlcdDisplay::clear(bool white) {
  if (buffer_ == nullptr) {
    return;
  }

  std::memset(buffer_, white ? kWhiteByte : kBlackByte, bufferLen_);
}

void RlcdDisplay::setPixel(int x, int y, bool black) {
  if (buffer_ == nullptr || x < 0 || y < 0 || x >= BoardConfig::RlcdWidth || y >= BoardConfig::RlcdHeight) {
    return;
  }

  writePixelUnchecked(buffer_, x, y, black);
}

void RlcdDisplay::invertPixel(int x, int y) {
  if (buffer_ == nullptr || x < 0 || y < 0 || x >= BoardConfig::RlcdWidth || y >= BoardConfig::RlcdHeight) {
    return;
  }

  uint8_t mask = 0;
  const uint32_t index = pixelIndex(x, y, mask);
  buffer_[index] ^= mask;
}

void RlcdDisplay::drawLine(int x0, int y0, int x1, int y1, bool black) {
  if (y0 == y1) {
    drawFastHLine(std::min(x0, x1), y0, std::abs(x1 - x0) + 1, black);
    return;
  }

  if (x0 == x1) {
    drawFastVLine(x0, std::min(y0, y1), std::abs(y1 - y0) + 1, black);
    return;
  }

  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    setPixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void RlcdDisplay::drawFastHLine(int x, int y, int w, bool black) {
  if (buffer_ == nullptr || w <= 0 || y < 0 || y >= BoardConfig::RlcdHeight || x >= BoardConfig::RlcdWidth) {
    return;
  }

  int xStart = std::max(0, x);
  int xEnd = std::min(BoardConfig::RlcdWidth, x + w);
  if (xStart >= xEnd) {
    return;
  }

  const uint16_t invY = BoardConfig::RlcdHeight - 1 - y;
  const uint16_t blockY = invY >> 2;
  const uint8_t localY = invY & 0x03;
  const uint8_t pairMask = static_cast<uint8_t>(0xC0 >> (localY << 1));
  const uint16_t stride = BoardConfig::RlcdHeight >> 2;

  if ((xStart & 0x01) != 0) {
    writePixelUnchecked(buffer_, xStart, y, black);
    ++xStart;
  }

  while (xStart + 1 < xEnd) {
    const uint32_t index = static_cast<uint32_t>(xStart >> 1) * stride + blockY;
    if (black) {
      buffer_[index] &= ~pairMask;
    } else {
      buffer_[index] |= pairMask;
    }
    xStart += 2;
  }

  if (xStart < xEnd) {
    writePixelUnchecked(buffer_, xStart, y, black);
  }
}

void RlcdDisplay::drawFastVLine(int x, int y, int h, bool black) {
  if (buffer_ == nullptr || h <= 0 || x < 0 || x >= BoardConfig::RlcdWidth || y >= BoardConfig::RlcdHeight) {
    return;
  }

  int yStart = std::max(0, y);
  int yEnd = std::min(BoardConfig::RlcdHeight, y + h);
  for (int yy = yStart; yy < yEnd; ++yy) {
    writePixelUnchecked(buffer_, x, yy, black);
  }
}

void RlcdDisplay::drawRect(int x, int y, int w, int h, bool black) {
  if (w <= 0 || h <= 0) {
    return;
  }

  drawFastHLine(x, y, w, black);
  drawFastHLine(x, y + h - 1, w, black);
  drawFastVLine(x, y, h, black);
  drawFastVLine(x + w - 1, y, h, black);
}

void RlcdDisplay::fillRect(int x, int y, int w, int h, bool black) {
  if (w <= 0 || h <= 0) {
    return;
  }

  const int xStart = std::max(0, x);
  const int yStart = std::max(0, y);
  const int xEnd = std::min(BoardConfig::RlcdWidth, x + w);
  const int yEnd = std::min(BoardConfig::RlcdHeight, y + h);

  for (int yy = yStart; yy < yEnd; ++yy) {
    drawFastHLine(xStart, yy, xEnd - xStart, black);
  }
}

void RlcdDisplay::drawCircle(int x0, int y0, int r, bool black) {
  if (r < 0) {
    return;
  }

  int f = 1 - r;
  int ddFx = 1;
  int ddFy = -2 * r;
  int x = 0;
  int y = r;

  setPixel(x0, y0 + r, black);
  setPixel(x0, y0 - r, black);
  setPixel(x0 + r, y0, black);
  setPixel(x0 - r, y0, black);

  while (x < y) {
    if (f >= 0) {
      --y;
      ddFy += 2;
      f += ddFy;
    }
    ++x;
    ddFx += 2;
    f += ddFx;

    setPixel(x0 + x, y0 + y, black);
    setPixel(x0 - x, y0 + y, black);
    setPixel(x0 + x, y0 - y, black);
    setPixel(x0 - x, y0 - y, black);
    setPixel(x0 + y, y0 + x, black);
    setPixel(x0 - y, y0 + x, black);
    setPixel(x0 + y, y0 - x, black);
    setPixel(x0 - y, y0 - x, black);
  }
}

void RlcdDisplay::fillCircle(int x0, int y0, int r, bool black) {
  if (r < 0) {
    return;
  }

  drawFastVLine(x0, y0 - r, 2 * r + 1, black);
  fillCircleHelper(x0, y0, r, 0x03, 0, black);
}

void RlcdDisplay::drawRoundRect(int x, int y, int w, int h, int r, bool black) {
  if (w <= 0 || h <= 0) {
    return;
  }

  r = std::max(0, std::min(r, std::min(w, h) / 2));
  drawFastHLine(x + r, y, w - 2 * r, black);
  drawFastHLine(x + r, y + h - 1, w - 2 * r, black);
  drawFastVLine(x, y + r, h - 2 * r, black);
  drawFastVLine(x + w - 1, y + r, h - 2 * r, black);
  drawCircleHelper(x + r, y + r, r, 0x01, black);
  drawCircleHelper(x + w - r - 1, y + r, r, 0x02, black);
  drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 0x04, black);
  drawCircleHelper(x + r, y + h - r - 1, r, 0x08, black);
}

void RlcdDisplay::fillRoundRect(int x, int y, int w, int h, int r, bool black) {
  if (w <= 0 || h <= 0) {
    return;
  }

  r = std::max(0, std::min(r, std::min(w, h) / 2));
  fillRect(x + r, y, w - 2 * r, h, black);
  fillCircleHelper(x + w - r - 1, y + r, r, 0x01, h - 2 * r - 1, black);
  fillCircleHelper(x + r, y + r, r, 0x02, h - 2 * r - 1, black);
}

void RlcdDisplay::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool black) {
  drawLine(x0, y0, x1, y1, black);
  drawLine(x1, y1, x2, y2, black);
  drawLine(x2, y2, x0, y0, black);
}

void RlcdDisplay::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool black) {
  if (y0 > y1) {
    std::swap(y0, y1);
    std::swap(x0, x1);
  }
  if (y1 > y2) {
    std::swap(y1, y2);
    std::swap(x1, x2);
  }
  if (y0 > y1) {
    std::swap(y0, y1);
    std::swap(x0, x1);
  }

  if (y0 == y2) {
    const int minX = std::min({x0, x1, x2});
    const int maxX = std::max({x0, x1, x2});
    drawFastHLine(minX, y0, maxX - minX + 1, black);
    return;
  }

  const int dx01 = x1 - x0;
  const int dy01 = y1 - y0;
  const int dx02 = x2 - x0;
  const int dy02 = y2 - y0;
  const int dx12 = x2 - x1;
  const int dy12 = y2 - y1;
  int sa = 0;
  int sb = 0;

  int y = y0;
  const int last = (y1 == y2) ? y1 : y1 - 1;
  for (; y <= last; ++y) {
    const int a = x0 + (dy01 == 0 ? 0 : sa / dy01);
    const int b = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    drawFastHLine(std::min(a, b), y, std::abs(a - b) + 1, black);
  }

  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for (; y <= y2; ++y) {
    const int a = x1 + (dy12 == 0 ? 0 : sa / dy12);
    const int b = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    drawFastHLine(std::min(a, b), y, std::abs(a - b) + 1, black);
  }
}

void RlcdDisplay::drawBitmap(int x, int y, const uint8_t* bitmap, int w, int h, bool blackBits) {
  if (bitmap == nullptr || w <= 0 || h <= 0) {
    return;
  }

  const int stride = (w + 7) / 8;
  for (int yy = 0; yy < h; ++yy) {
    for (int xx = 0; xx < w; ++xx) {
      const uint8_t byte = bitmap[yy * stride + (xx >> 3)];
      const bool bitSet = (byte & (0x80 >> (xx & 0x07))) != 0;
      if (bitSet) {
        setPixel(x + xx, y + yy, blackBits);
      }
    }
  }
}

void RlcdDisplay::drawText(int x, int y, const char* text, bool black, int scale) {
  if (text == nullptr) {
    return;
  }

  scale = std::max(1, scale);
  int cursorX = x;
  int cursorY = y;

  while (*text != '\0') {
    if (*text == '\n') {
      cursorX = x;
      cursorY += (PixelFont5x7::Height + 1) * scale;
    } else {
      drawChar(cursorX, cursorY, *text, black, scale);
      cursorX += PixelFont5x7::Advance * scale;
    }
    ++text;
  }
}

void RlcdDisplay::flushFull() {
  if (!ready_ || buffer_ == nullptr) {
    return;
  }

  sendCommand(0x2A);
  sendData(0x12);
  sendData(0x2A);
  sendCommand(0x2B);
  sendData(0x00);
  sendData(0xC7);
  sendCommand(0x2C);
  sendBuffer(buffer_, bufferLen_);
}

void RlcdDisplay::reset() {
  setResetLevel(true);
  delay(50);
  setResetLevel(false);
  delay(20);
  setResetLevel(true);
  delay(50);
}

void RlcdDisplay::sendCommand(uint8_t command) {
  esp_lcd_panel_io_tx_param(g_ioHandle, command, nullptr, 0);
}

void RlcdDisplay::sendData(uint8_t data) {
  esp_lcd_panel_io_tx_param(g_ioHandle, -1, &data, 1);
}

void RlcdDisplay::sendBuffer(const uint8_t* data, int len) {
  esp_lcd_panel_io_tx_color(g_ioHandle, -1, data, len);
}

void RlcdDisplay::initPanel() {
  reset();

  sendCommand(0xD6);
  sendData(0x17);
  sendData(0x02);
  sendCommand(0xD1);
  sendData(0x01);
  sendCommand(0xC0);
  sendData(0x11);
  sendData(0x04);
  sendCommand(0xC1);
  sendData(0x69);
  sendData(0x69);
  sendData(0x69);
  sendData(0x69);
  sendCommand(0xC2);
  sendData(0x19);
  sendData(0x19);
  sendData(0x19);
  sendData(0x19);
  sendCommand(0xC4);
  sendData(0x4B);
  sendData(0x4B);
  sendData(0x4B);
  sendData(0x4B);
  sendCommand(0xC5);
  sendData(0x19);
  sendData(0x19);
  sendData(0x19);
  sendData(0x19);
  sendCommand(0xD8);
  sendData(0x80);
  sendData(0xE9);
  sendCommand(0xB2);
  sendData(0x02);
  sendCommand(0xB3);
  sendData(0xE5);
  sendData(0xF6);
  sendData(0x05);
  sendData(0x46);
  sendData(0x77);
  sendData(0x77);
  sendData(0x77);
  sendData(0x77);
  sendData(0x76);
  sendData(0x45);
  sendCommand(0xB4);
  sendData(0x05);
  sendData(0x46);
  sendData(0x77);
  sendData(0x77);
  sendData(0x77);
  sendData(0x77);
  sendData(0x76);
  sendData(0x45);
  sendCommand(0x62);
  sendData(0x32);
  sendData(0x03);
  sendData(0x1F);
  sendCommand(0xB7);
  sendData(0x13);
  sendCommand(0xB0);
  sendData(0x64);
  sendCommand(0x11);
  delay(200);
  sendCommand(0xC9);
  sendData(0x00);
  sendCommand(0x36);
  sendData(0x48);
  sendCommand(0x3A);
  sendData(0x11);
  sendCommand(0xB9);
  sendData(0x20);
  sendCommand(0xB8);
  sendData(0x29);
  sendCommand(0x21);
  sendCommand(0x2A);
  sendData(0x12);
  sendData(0x2A);
  sendCommand(0x2B);
  sendData(0x00);
  sendData(0xC7);
  sendCommand(0x35);
  sendData(0x00);
  sendCommand(0xD0);
  sendData(0xFF);
  sendCommand(0x38);
  sendCommand(0x29);

  clear(true);
  flushFull();
}

void RlcdDisplay::releaseBuffer() {
  if (buffer_ == nullptr) {
    return;
  }

  heap_caps_free(buffer_);
  buffer_ = nullptr;
  bufferLen_ = 0;
}

void RlcdDisplay::setResetLevel(bool high) {
  gpio_set_level(static_cast<gpio_num_t>(BoardConfig::RlcdRst), high ? 1 : 0);
}

void RlcdDisplay::drawChar(int x, int y, char c, bool black, int scale) {
  const uint8_t* rows = PixelFont5x7::glyphRows(c);
  for (int row = 0; row < PixelFont5x7::Height; ++row) {
    for (int col = 0; col < PixelFont5x7::Width; ++col) {
      if ((rows[row] & (0x10 >> col)) == 0) {
        continue;
      }

      if (scale == 1) {
        setPixel(x + col, y + row, black);
      } else {
        fillRect(x + col * scale, y + row * scale, scale, scale, black);
      }
    }
  }
}

void RlcdDisplay::drawCircleHelper(int x0, int y0, int r, uint8_t cornerMask, bool black) {
  int f = 1 - r;
  int ddFx = 1;
  int ddFy = -2 * r;
  int x = 0;
  int y = r;

  while (x < y) {
    if (f >= 0) {
      --y;
      ddFy += 2;
      f += ddFy;
    }
    ++x;
    ddFx += 2;
    f += ddFx;

    if (cornerMask & 0x01) {
      setPixel(x0 - y, y0 - x, black);
      setPixel(x0 - x, y0 - y, black);
    }
    if (cornerMask & 0x02) {
      setPixel(x0 + x, y0 - y, black);
      setPixel(x0 + y, y0 - x, black);
    }
    if (cornerMask & 0x04) {
      setPixel(x0 + x, y0 + y, black);
      setPixel(x0 + y, y0 + x, black);
    }
    if (cornerMask & 0x08) {
      setPixel(x0 - y, y0 + x, black);
      setPixel(x0 - x, y0 + y, black);
    }
  }
}

void RlcdDisplay::fillCircleHelper(int x0, int y0, int r, uint8_t cornerMask, int delta, bool black) {
  int f = 1 - r;
  int ddFx = 1;
  int ddFy = -2 * r;
  int x = 0;
  int y = r;

  while (x < y) {
    if (f >= 0) {
      --y;
      ddFy += 2;
      f += ddFy;
    }
    ++x;
    ddFx += 2;
    f += ddFx;

    if (cornerMask & 0x01) {
      drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, black);
      drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, black);
    }
    if (cornerMask & 0x02) {
      drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, black);
      drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, black);
    }
  }
}
