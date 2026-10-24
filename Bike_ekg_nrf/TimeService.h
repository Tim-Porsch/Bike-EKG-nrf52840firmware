#pragma once

// 1-MHz-Timer für relative Messzeiten; Unixzeit ist eine separat gesetzte Referenz.
// uint32-Mikrosekunden laufen nach ca. 71,6 Minuten über (modulare Differenzen).

#include <Arduino.h>

class TimeService {
public:
  bool begin();
  bool isClockRunning() const;
  void setUnixTime(uint32_t unixSeconds);
  bool hasUnixTime() const;
  uint32_t unixTime() const;
  uint32_t bootTimeMs() const;
  uint32_t bootTimeUs() const;
  uint32_t bootTimeUsFromIsr() const;

private:
  bool clockRunning_ = false;
  bool hasUnixTime_ = false;
  uint32_t unixTimeAtSet_ = 0;
  uint32_t bootTimeAtSetMs_ = 0;
};
