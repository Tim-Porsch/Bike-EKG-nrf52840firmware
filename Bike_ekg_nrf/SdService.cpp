#include "SdService.h"

#include <SPI.h>

namespace {
constexpr uint8_t SD_MISO_PIN = 1;  // P0.24
constexpr uint8_t SD_SCK_PIN = 28;  // P0.20
constexpr uint8_t SD_MOSI_PIN = 30; // P0.22

constexpr uint32_t SD_CS_MASK = 1UL << 11;    // P1.11, active low
constexpr uint32_t SD_DETECT_MASK = 1UL << 2; // P1.02, active high
constexpr uint32_t SD_POWER_MASK = 1UL << 10; // P0.10, active high
} // namespace

// SdFat is configured to call these functions because P1.11 is not an Arduino
// pin for the selected Feather board variant.
void sdCsInit(SdCsPin_t) {
  NRF_P1->OUTSET = SD_CS_MASK;
  NRF_P1->DIRSET = SD_CS_MASK;
}

void sdCsWrite(SdCsPin_t, bool high) {
  if (high) {
    NRF_P1->OUTSET = SD_CS_MASK;
  } else {
    NRF_P1->OUTCLR = SD_CS_MASK;
  }
}

bool SdService::begin() {
  if (initialized_) {
    return true;
  }

  configurePins();
  // Adafruit 4682 DET is pulled up to the switched SD supply. It cannot
  // indicate an inserted card reliably until that supply has settled.
  NRF_P0->OUTSET = SD_POWER_MASK;
  delay(500);
  if ((NRF_P1->IN & SD_DETECT_MASK) == 0) {
    NRF_P0->OUTCLR = SD_POWER_MASK;
    lastError_ = "SD card not detected";
    return false;
  }

  SPI.setPins(SD_MISO_PIN, SD_SCK_PIN, SD_MOSI_PIN);
  if (!sd_.begin(SdSpiConfig(0, SHARED_SPI, SD_SCK_MHZ(4)))) {
    NRF_P0->OUTCLR = SD_POWER_MASK;
    lastError_ = "SD initialization failed";
    return false;
  }

  initialized_ = true;
  lastError_ = "none";
  return true;
}

void SdService::end() {
  if (!initialized_) {
    return;
  }

  sd_.end();
  NRF_P0->OUTCLR = SD_POWER_MASK;
  initialized_ = false;
}

bool SdService::openNextLogFile(FsFile& file, char* fileName,
                                size_t fileNameSize) {
  // Nie vorhandene Messungen überschreiben: freien Namen suchen und exklusiv anlegen.
  if (!initialized_) {
    lastError_ = "SD is not initialized";
    return false;
  }

  for (uint32_t index = 0; index <= 99999; index++) {
    snprintf(fileName, fileNameSize, "EKG%05lu.BIN",
             static_cast<unsigned long>(index));
    if (sd_.exists(fileName)) {
      continue;
    }
    if (file.open(fileName, O_RDWR | O_CREAT | O_EXCL)) {
      lastError_ = "none";
      return true;
    }

    lastError_ = "could not create log file";
    return false;
  }

  lastError_ = "no free log file name";
  return false;
}

const char* SdService::lastError() const {
  return lastError_;
}

bool SdService::openRoot(FsFile& directory) {
  return initialized_ && directory.open("/", O_RDONLY);
}

bool SdService::openRead(FsFile& file, const char* name) {
  return initialized_ && file.open(name, O_RDONLY);
}

void SdService::configurePins() {
  NRF_P0->OUTCLR = SD_POWER_MASK;
  NRF_P0->DIRSET = SD_POWER_MASK;
  NRF_P1->PIN_CNF[2] = 0;
  sdCsInit(0);
}
