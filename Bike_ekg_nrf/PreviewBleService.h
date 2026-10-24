#pragma once

// Browservorschau mit Status- und Datencharacteristic; kein verlustfreier Rekorder.
// Session-ID und Quellenzähler trennen Neustarts von tatsächlichen Datenlücken.

#include <bluefruit.h>
#include "PreviewBuffer.h"

class PreviewBleService {
public:
  PreviewBleService();
  void begin();
  bool subscribed();
  void start(uint16_t rate, bool filtered);
  void append(uint32_t number, int32_t value);
  void setStatistics(uint32_t sourceEnd, uint32_t acquisitionDropped);
  void stop();
  void update();

private:
  BLEService service_;
  BLECharacteristic data_;
  BLECharacteristic status_;
  PreviewBuffer buffer_;
  uint16_t session_ = 0;
  uint16_t rate_ = 0;
  uint32_t sourceEnd_ = 0;
  uint32_t acquisitionDropped_ = 0;
  uint32_t lastFlushMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  bool active_ = false;
  bool filtered_ = false;
  bool statusDirty_ = true;

  uint8_t samplesPerFrame();
  void writeStatus();
};
