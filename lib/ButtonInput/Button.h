#pragma once

#include <Arduino.h>

class Button {
public:
  explicit Button(int pin, bool activeLow = true);

  void begin();
  void update();

  bool isPressed() const;
  bool consumePressed();
  bool consumeReleased();
  uint32_t lastPressDurationMs() const;
  uint32_t currentPressDurationMs() const;

private:
  bool pressedLevel() const;
  bool readRaw() const;

  int pin_;
  bool activeLow_;
  static constexpr uint32_t kDebounceMs = 30;

  bool lastRaw_ = HIGH;
  bool lastStable_ = HIGH;
  uint32_t lastChangeMs_ = 0;
  uint32_t pressStartMs_ = 0;
  uint32_t lastPressDurationMs_ = 0;
  bool pressedEvent_ = false;
  bool releasedEvent_ = false;
};
