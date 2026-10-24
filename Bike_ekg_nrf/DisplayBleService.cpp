#include "DisplayBleService.h"

namespace {
constexpr char DISPLAY_SERVICE_UUID[] =
    "7f510001-1212-efde-1523-785feabcd123";
constexpr char DISPLAY_DATA_UUID[] =
    "7f510002-1212-efde-1523-785feabcd123";
constexpr char DISPLAY_CONFIG_UUID[] =
    "7f510003-1212-efde-1523-785feabcd123";

constexpr uint8_t CONFIG_FLAG_ACTIVE = 1 << 0;
constexpr uint8_t CONFIG_FLAG_VALID = 1 << 1;
constexpr uint8_t CONFIG_FLAG_FILTER_WARMING = 1 << 2;
constexpr uint8_t CONFIG_FLAG_NON_MEDICAL = 1 << 6;
} // namespace

DisplayBleService::DisplayBleService()
    : service_(DISPLAY_SERVICE_UUID), dataCharacteristic_(DISPLAY_DATA_UUID),
      configCharacteristic_(DISPLAY_CONFIG_UUID) {}

void DisplayBleService::begin() {
  service_.begin();

  dataCharacteristic_.setProperties(CHR_PROPS_NOTIFY);
  dataCharacteristic_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  dataCharacteristic_.setMaxLen(MAX_DATA_FRAME_BYTES);
  dataCharacteristic_.begin();

  configCharacteristic_.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  configCharacteristic_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  configCharacteristic_.setFixedLen(CONFIG_FRAME_BYTES);
  configCharacteristic_.begin();

  uint8_t config[CONFIG_FRAME_BYTES] = {};
  rebuildConfig(config);
  configCharacteristic_.write(config, sizeof(config));
}

BLEService& DisplayBleService::service() {
  return service_;
}

void DisplayBleService::start(uint16_t sampleRateSps,
                              uint16_t filterLowCutoffMilliHz,
                              uint16_t filterHighCutoffMilliHz) {
  sessionId_++;
  frameSequence_ = 0;
  sampleRateSps_ = sampleRateSps;
  filterLowCutoffMilliHz_ = filterLowCutoffMilliHz;
  filterHighCutoffMilliHz_ = filterHighCutoffMilliHz;
  active_ = true;
  valid_ = false;
  configDirty_ = true;
  segmentGapPending_ = true;
  txDroppedPending_ = false;
  partialCount_ = 0;
  clearFrames(false);
}

void DisplayBleService::setValid(bool valid) {
  if (valid_ == valid) {
    return;
  }

  valid_ = valid;
  configDirty_ = true;
}

void DisplayBleService::restartSegment() {
  // Nach einer Lücke ist die Filtervorgeschichte ungültig: alten Rückstau verwerfen
  // und erst nach erneutem Einschwingen wieder gültige Samples freigeben.
  partialCount_ = 0;
  segmentGapPending_ = true;
  setValid(false);
  clearFrames(true);
}

void DisplayBleService::appendSample(uint32_t sampleNumber, int32_t sample) {
  if (!active_ || !valid_) {
    return;
  }

  if (partialCount_ == 0) {
    firstSampleNumber_ = sampleNumber;
  }
  partialSamples_[partialCount_++] = sample;
  if (partialCount_ == samplesPerFrame()) {
    enqueueFrame();
  }
}

void DisplayBleService::update() {
  if (configDirty_) {
    uint8_t config[CONFIG_FRAME_BYTES] = {};
    rebuildConfig(config);
    configCharacteristic_.write(config, sizeof(config));
    if (configCharacteristic_.notifyEnabled()) {
      configCharacteristic_.notify(config, sizeof(config));
    }
    configDirty_ = false;
  }

  if (!dataCharacteristic_.notifyEnabled()) {
    clearFrames(true);
    return;
  }
  if (queueCount_ == 0) {
    return;
  }

  QueuedFrame& frame = frames_[queueTail_];
  if (!dataCharacteristic_.notify(frame.bytes, frame.length)) {
    // The SoftDevice buffer is full for this connection event. Keep the frame
    // and retry after it has transmitted queued notifications.
    return;
  }

  queueTail_ = (queueTail_ + 1) % FRAME_QUEUE_CAPACITY;
  queueCount_--;
}

void DisplayBleService::rebuildConfig(uint8_t config[CONFIG_FRAME_BYTES]) const {
  config[0] = CONFIG_TYPE;
  config[1] = PROTOCOL_VERSION;
  config[2] = sessionId_;
  config[3] = CONFIG_FLAG_NON_MEDICAL;
  if (active_) {
    config[3] |= CONFIG_FLAG_ACTIVE;
  }
  if (valid_) {
    config[3] |= CONFIG_FLAG_VALID;
  } else if (active_) {
    config[3] |= CONFIG_FLAG_FILTER_WARMING;
  }
  putU16(&config[4], sampleRateSps_);
  config[6] = MAX_SAMPLES_PER_FRAME;
  config[7] = 2; // Signed int32 samples with the configured bandpass.
  putU16(&config[8], filterLowCutoffMilliHz_);
  putU16(&config[10], filterHighCutoffMilliHz_);
  putU16(&config[12], MAX_DATA_FRAME_BYTES);
  config[14] = SAMPLE_BYTES;
  config[15] = DATA_FRAME_HEADER_BYTES;
  putU16(&config[18], 500);
}

void DisplayBleService::enqueueFrame() {
  // Aktualität vor Vollständigkeit: bei Rückstau das älteste Paket verwerfen.
  // Das Flag informiert den Empfänger; dieser Dienst ersetzt keinen SD-Rekorder.
  if (queueCount_ == FRAME_QUEUE_CAPACITY) {
    queueTail_ = (queueTail_ + 1) % FRAME_QUEUE_CAPACITY;
    queueCount_--;
    txDroppedPending_ = true;
  }

  QueuedFrame& frame = frames_[queueHead_];
  frame.bytes[0] = DATA_TYPE;
  frame.bytes[1] = PROTOCOL_VERSION;
  frame.bytes[2] = sessionId_;
  frame.bytes[3] = DATA_FLAG_VALID | DATA_FLAG_BANDPASS;
  if (segmentGapPending_) {
    frame.bytes[3] |= DATA_FLAG_GAP;
    segmentGapPending_ = false;
  }
  if (txDroppedPending_) {
    frame.bytes[3] |= DATA_FLAG_TX_DROPPED;
    txDroppedPending_ = false;
  }
  putU16(&frame.bytes[4], frameSequence_++);
  putU32(&frame.bytes[6], firstSampleNumber_);
  frame.bytes[10] = partialCount_;
  for (uint8_t index = 0; index < partialCount_; index++) {
    putU32(&frame.bytes[DATA_FRAME_HEADER_BYTES + index * SAMPLE_BYTES],
           static_cast<uint32_t>(partialSamples_[index]));
  }
  frame.length = DATA_FRAME_HEADER_BYTES + partialCount_ * SAMPLE_BYTES;

  queueHead_ = (queueHead_ + 1) % FRAME_QUEUE_CAPACITY;
  queueCount_++;
  partialCount_ = 0;
}

void DisplayBleService::clearFrames(bool markDropped) {
  if (markDropped && (queueCount_ > 0 || partialCount_ > 0)) {
    txDroppedPending_ = true;
  }
  queueHead_ = 0;
  queueTail_ = 0;
  queueCount_ = 0;
  partialCount_ = 0;
}

uint8_t DisplayBleService::samplesPerFrame() const {
  uint16_t notificationBytes = MAX_DATA_FRAME_BYTES;
  if (Bluefruit.Periph.connected()) {
    BLEConnection* connection = Bluefruit.Connection(Bluefruit.connHandle());
    if (connection != nullptr && connection->getMtu() > 3) {
      notificationBytes = min(notificationBytes,
                              static_cast<uint16_t>(connection->getMtu() - 3));
    }
  }
  const uint16_t samples =
      (notificationBytes - DATA_FRAME_HEADER_BYTES) / SAMPLE_BYTES;
  return samples == 0 ? 1 : static_cast<uint8_t>(samples);
}

void DisplayBleService::putU16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void DisplayBleService::putU32(uint8_t* destination, uint32_t value) {
  for (uint8_t index = 0; index < 4; index++) {
    destination[index] = static_cast<uint8_t>(value >> (index * 8));
  }
}
