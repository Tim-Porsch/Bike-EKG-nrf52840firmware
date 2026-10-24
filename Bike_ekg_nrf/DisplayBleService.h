#pragma once

// Gefilterter Display-Datenstrom, getrennt von Browservorschau und SD-Transfer.
// Paketdefinitionen müssen mit ESP32_Display und display_protocol.py übereinstimmen.

#include <bluefruit.h>

class DisplayBleService {
public:
  static constexpr uint8_t CONFIG_FRAME_BYTES = 20;
  static constexpr uint16_t MAX_DATA_FRAME_BYTES = 243;
  static constexpr uint8_t DATA_FRAME_HEADER_BYTES = 11;
  static constexpr uint8_t SAMPLE_BYTES = 4;
  static constexpr uint8_t MAX_SAMPLES_PER_FRAME =
      (MAX_DATA_FRAME_BYTES - DATA_FRAME_HEADER_BYTES) / SAMPLE_BYTES;

  DisplayBleService();

  void begin();
  BLEService& service();
  void start(uint16_t sampleRateSps, uint16_t filterLowCutoffMilliHz,
             uint16_t filterHighCutoffMilliHz);
  void setValid(bool valid);
  void restartSegment();
  void appendSample(uint32_t sampleNumber, int32_t sample);
  void update();

private:
  static constexpr uint8_t FRAME_QUEUE_CAPACITY = 8;
  static constexpr uint8_t CONFIG_TYPE = 0xC0;
  static constexpr uint8_t DATA_TYPE = 0xD0;
  static constexpr uint8_t PROTOCOL_VERSION = 2;
  static constexpr uint8_t DATA_FLAG_VALID = 1 << 0;
  static constexpr uint8_t DATA_FLAG_GAP = 1 << 1;
  static constexpr uint8_t DATA_FLAG_TX_DROPPED = 1 << 2;
  static constexpr uint8_t DATA_FLAG_BANDPASS = 1 << 4;

  struct QueuedFrame {
    uint8_t bytes[MAX_DATA_FRAME_BYTES];
    uint16_t length;
  };

  BLEService service_;
  BLECharacteristic dataCharacteristic_;
  BLECharacteristic configCharacteristic_;
  QueuedFrame frames_[FRAME_QUEUE_CAPACITY] = {};
  int32_t partialSamples_[MAX_SAMPLES_PER_FRAME] = {};
  uint8_t partialCount_ = 0;
  uint8_t queueHead_ = 0;
  uint8_t queueTail_ = 0;
  uint8_t queueCount_ = 0;
  uint8_t sessionId_ = 0;
  uint16_t frameSequence_ = 0;
  uint32_t firstSampleNumber_ = 0;
  uint16_t sampleRateSps_ = 0;
  uint16_t filterLowCutoffMilliHz_ = 500;
  uint16_t filterHighCutoffMilliHz_ = 40000;
  bool active_ = false;
  bool valid_ = false;
  bool configDirty_ = false;
  bool segmentGapPending_ = false;
  bool txDroppedPending_ = false;

  void rebuildConfig(uint8_t config[CONFIG_FRAME_BYTES]) const;
  void enqueueFrame();
  void clearFrames(bool markDropped);
  uint8_t samplesPerFrame() const;
  static void putU16(uint8_t* destination, uint16_t value);
  static void putU32(uint8_t* destination, uint32_t value);
};
