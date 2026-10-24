#include "PreviewBleService.h"

PreviewBleService::PreviewBleService()
    : service_("7f520001-1212-efde-1523-785feabcd123"),
      data_("7f520002-1212-efde-1523-785feabcd123"),
      status_("7f520003-1212-efde-1523-785feabcd123") {}

void PreviewBleService::begin() {
  service_.begin();
  data_.setProperties(CHR_PROPS_NOTIFY);
  data_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  data_.setMaxLen(PreviewBuffer::MAX_BYTES);
  data_.begin();
  status_.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  status_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  status_.setFixedLen(20);
  status_.begin();
  writeStatus();
}

bool PreviewBleService::subscribed() {
  return Bluefruit.Periph.connected() && data_.notifyEnabled();
}

void PreviewBleService::start(uint16_t rate, bool filtered) {
  // Neue Sitzung: Puffer und Verluststatistik dürfen keine Vorgeschichte übernehmen.
  buffer_.reset(++session_);
  rate_ = rate;
  filtered_ = filtered;
  sourceEnd_ = acquisitionDropped_ = 0;
  active_ = true;
  statusDirty_ = true;
  lastFlushMs_ = lastStatusMs_ = millis();
  writeStatus();
}

void PreviewBleService::append(uint32_t number, int32_t value) {
  if (active_) buffer_.append(number, value, samplesPerFrame());
}

void PreviewBleService::setStatistics(uint32_t sourceEnd, uint32_t acquisitionDropped) {
  sourceEnd_ = sourceEnd;
  acquisitionDropped_ = acquisitionDropped;
}

void PreviewBleService::stop() {
  active_ = false;
  // No backlog may leak into logging or a later preview session. Final status
  // includes these discarded tail samples, even if no further data frame arrives.
  buffer_.discard();
  statusDirty_ = true;
  writeStatus();
}

void PreviewBleService::update() {
  const uint32_t now = millis();
  if (active_ && now - lastFlushMs_ >= 40) {
    buffer_.flush();
    lastFlushMs_ = now;
  }
  if (statusDirty_ || (active_ && now - lastStatusMs_ >= 1000)) writeStatus();
  if (!active_ || !subscribed()) return;
  // One frame per loop, keeping the backlog bounded. Bluefruit handles ATT TX
  // credits internally; acquisition losses during radio backpressure are counted.
  const PreviewBuffer::Frame* frame = buffer_.front();
  if (frame && data_.notify(frame->bytes, frame->length)) buffer_.pop();
}

uint8_t PreviewBleService::samplesPerFrame() {
  uint16_t bytes = 20; // ATT MTU 23 must work without browser MTU control.
  BLEConnection* connection = Bluefruit.Connection(Bluefruit.connHandle());
  if (connection && connection->getMtu() >= 23) {
    bytes = min(static_cast<uint16_t>(PreviewBuffer::MAX_BYTES),
                static_cast<uint16_t>(connection->getMtu() - 3));
  }
  return static_cast<uint8_t>((bytes - PreviewBuffer::HEADER_BYTES) / 4);
}

void PreviewBleService::writeStatus() {
  // C1, version, session:u16, rate:u16, active:u8, format(1=raw, 2=filtered int32),
  // sourceEnd:u32 (exclusive), acquisitionDropped:u32, txDroppedSamples:u32.
  // Acquisition counters overlap missing sample numbers; do not add them twice.
  uint8_t bytes[20] = {0xC1, 1};
  PreviewBuffer::put16(bytes + 2, session_);
  PreviewBuffer::put16(bytes + 4, rate_);
  bytes[6] = active_ ? 1 : 0;
  bytes[7] = filtered_ ? 2 : 1;
  PreviewBuffer::put32(bytes + 8, sourceEnd_);
  PreviewBuffer::put32(bytes + 12, acquisitionDropped_);
  PreviewBuffer::put32(bytes + 16, buffer_.dropped());
  status_.write(bytes, sizeof(bytes));
  statusDirty_ = status_.notifyEnabled() && !status_.notify(bytes, sizeof(bytes));
  lastStatusMs_ = millis();
}
