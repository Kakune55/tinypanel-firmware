#pragma once

#include <Arduino.h>
#include <cstdint>

class RlcdDisplay {
public:
  static constexpr int Width = 400;
  static constexpr int Height = 300;

  bool begin();
  bool isReady() const;
  int width() const;
  int height() const;

  void clear(bool white = true);
  void setPixel(int x, int y, bool black);
  void invertPixel(int x, int y);
  void drawLine(int x0, int y0, int x1, int y1, bool black);
  void drawFastHLine(int x, int y, int w, bool black);
  void drawFastVLine(int x, int y, int h, bool black);
  void drawRect(int x, int y, int w, int h, bool black);
  void fillRect(int x, int y, int w, int h, bool black);
  void drawCircle(int x0, int y0, int r, bool black);
  void fillCircle(int x0, int y0, int r, bool black);
  void drawRoundRect(int x, int y, int w, int h, int r, bool black);
  void fillRoundRect(int x, int y, int w, int h, int r, bool black);
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool black);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool black);
  void drawBitmap(int x, int y, const uint8_t* bitmap, int w, int h, bool blackBits = true);
  void drawText(int x, int y, const char* text, bool black = true, int scale = 1);
  void flushFull();

private:
  void reset();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendBuffer(const uint8_t* data, int len);
  void initPanel();
  void setResetLevel(bool high);
  void drawChar(int x, int y, char c, bool black, int scale);
  void drawCircleHelper(int x0, int y0, int r, uint8_t cornerMask, bool black);
  void fillCircleHelper(int x0, int y0, int r, uint8_t cornerMask, int delta, bool black);

  uint8_t* buffer_ = nullptr;
  int bufferLen_ = 0;
  bool ready_ = false;
};
