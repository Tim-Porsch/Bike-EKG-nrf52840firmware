#pragma once
// Testdouble für diese PCB: Zeit vergeht virtuell, DET hängt von SD-Power ab.
// Kein allgemeiner nRF-GPIO- oder SPI-Emulator.
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

inline bool testPowered = false;
inline bool testCardInserted = true;
inline uint32_t testSettledMs = 0;
inline unsigned testDetectReads = 0;
inline unsigned testSpiSetups = 0;

struct PowerRegister {
  bool high;
  void operator=(uint32_t mask) {
    assert(mask == (1UL << 10));
    testPowered = high;
    testSettledMs = 0;
  }
};
struct DetectRegister {
  operator uint32_t() const {
    ++testDetectReads;
    // Model Adafruit 4682: DET's pull-up is supplied by switched SD_3V3.
    return testPowered && testSettledMs >= 500 && testCardInserted ? (1UL << 2) : 0;
  }
};
struct Port0 {
  PowerRegister OUTSET{true}, OUTCLR{false};
  uint32_t DIRSET = 0;
};
struct Port1 {
  uint32_t OUTSET = 0, OUTCLR = 0, DIRSET = 0, PIN_CNF[32] = {};
  DetectRegister IN;
};
inline Port0 testPort0;
inline Port1 testPort1;
#define NRF_P0 (&testPort0)
#define NRF_P1 (&testPort1)
inline void delay(uint32_t ms) { assert(testPowered); testSettledMs += ms; }
struct TestSPI {
  void setPins(uint8_t miso, uint8_t sck, uint8_t mosi) {
    assert(testPowered && testSettledMs >= 500);
    assert(miso == 1 && sck == 28 && mosi == 30);
    ++testSpiSetups;
  }
};
inline TestSPI SPI;
