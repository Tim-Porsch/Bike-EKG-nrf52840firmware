#pragma once
#include "Arduino.h"

class BluefruitStub {
public:
  bool active = true;
  bool connected() const { return active; }
};
extern BluefruitStub Bluefruit;

class BLEUart : public Stream {
public:
  bool subscribed = true;
  bool stalled = false;
  size_t packets = 0;
  size_t attempts = 0;
  uint32_t blockedMs = 0;
  std::string output;
  bool notifyEnabled() const { return subscribed; }
  size_t write(const uint8_t* data, size_t length) {
    ++attempts;
    if (stalled) {
      testMillis += blockedMs;
      return 0;
    }
    ++packets;
    output.append(reinterpret_cast<const char*>(data), length);
    return length;
  }
};
