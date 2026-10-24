#include "TimeService.h"

#include <nrf_soc.h>

bool TimeService::begin() {
  if (clockRunning_) {
    return true;
  }

  if (sd_clock_hfclk_request() != NRF_SUCCESS) {
    return false;
  }

  uint32_t hfclkRunning = 0;
  while (hfclkRunning == 0) {
    if (sd_clock_hfclk_is_running(&hfclkRunning) != NRF_SUCCESS) {
      return false;
    }
  }

  NRF_TIMER4->TASKS_STOP = 1;
  NRF_TIMER4->SHORTS = 0;
  NRF_TIMER4->INTENCLR = UINT32_MAX;
  NRF_TIMER4->MODE = TIMER_MODE_MODE_Timer;
  NRF_TIMER4->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
  NRF_TIMER4->PRESCALER = 4; // 16 MHz / 2^4 = 1 MHz.
  NRF_TIMER4->TASKS_CLEAR = 1;
  NRF_TIMER4->TASKS_START = 1;
  clockRunning_ = true;
  return true;
}

bool TimeService::isClockRunning() const {
  return clockRunning_;
}

void TimeService::setUnixTime(uint32_t unixSeconds) {
  unixTimeAtSet_ = unixSeconds;
  bootTimeAtSetMs_ = bootTimeMs();
  hasUnixTime_ = true;
}

bool TimeService::hasUnixTime() const {
  return hasUnixTime_;
}

uint32_t TimeService::unixTime() const {
  if (!hasUnixTime_) {
    return 0;
  }

  return unixTimeAtSet_ + (bootTimeMs() - bootTimeAtSetMs_) / 1000;
}

uint32_t TimeService::bootTimeMs() const {
  return bootTimeUs() / 1000;
}

uint32_t TimeService::bootTimeUs() const {
  if (!clockRunning_) {
    return millis() * 1000UL;
  }

  NRF_TIMER4->TASKS_CAPTURE[1] = 1;
  return NRF_TIMER4->CC[1];
}

uint32_t TimeService::bootTimeUsFromIsr() const {
  // CC[0] wurde von DRDY/PPI erfasst; hier nicht den späteren ISR-Zeitpunkt messen.
  if (!clockRunning_) {
    return 0;
  }

  return NRF_TIMER4->CC[0];
}
