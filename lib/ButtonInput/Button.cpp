#include "Button.h"

Button::Button(int pin, bool activeLow) : pin_(pin), activeLow_(activeLow) {}

void Button::begin() {
  pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT_PULLDOWN);
  lastRaw_ = readRaw();
  lastStable_ = lastRaw_;
  lastChangeMs_ = millis();
}

void Button::update() {
  const bool raw = readRaw();
  const uint32_t now = millis();

  if (raw != lastRaw_) {
    lastRaw_ = raw;
    lastChangeMs_ = now;
  }

  if (now - lastChangeMs_ <= kDebounceMs || raw == lastStable_) {
    return;
  }

  lastStable_ = raw;

  if (isPressed()) {
    pressedEvent_ = true;
    pressStartMs_ = now;
  } else {
    releasedEvent_ = true;
    lastPressDurationMs_ = now - pressStartMs_;
  }
}

bool Button::isPressed() const {
  return lastStable_ == pressedLevel();
}

bool Button::consumePressed() {
  if (!pressedEvent_) {
    return false;
  }

  pressedEvent_ = false;
  return true;
}

bool Button::consumeReleased() {
  if (!releasedEvent_) {
    return false;
  }

  releasedEvent_ = false;
  return true;
}

uint32_t Button::lastPressDurationMs() const {
  return lastPressDurationMs_;
}

uint32_t Button::currentPressDurationMs() const {
  return isPressed() ? millis() - pressStartMs_ : 0;
}

bool Button::pressedLevel() const {
  return activeLow_ ? LOW : HIGH;
}

bool Button::readRaw() const {
  return digitalRead(pin_);
}
