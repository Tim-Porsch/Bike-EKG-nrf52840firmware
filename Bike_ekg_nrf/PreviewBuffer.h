#pragma once

#include <stdint.h>

// Internal browser preview protocol, independent of the filtered display service.
// Data, little endian: D1, version(1), session:u16, sequence:u16,
// firstSample:u32, count:u8, reserved:u8, count signed int32 ADC values.
// Only consecutive source samples share a frame. Sequence/sample numbers wrap.
class PreviewBuffer {
public:
  static constexpr uint16_t MAX_BYTES = 244;
  static constexpr uint8_t HEADER_BYTES = 12;
  static constexpr uint8_t MAX_SAMPLES = (MAX_BYTES - HEADER_BYTES) / 4;
  static constexpr uint8_t CAPACITY = 8;

  struct Frame {
    uint8_t bytes[MAX_BYTES];
    uint16_t length;
  };

  void reset(uint16_t session) {
    session_ = session;
    sequence_ = 0;
    head_ = tail_ = size_ = count_ = 0;
    dropped_ = 0;
  }

  void append(uint32_t number, int32_t value, uint8_t limit) {
    // Eine Lücke beendet das Paket, da darin nur die erste Probennummer steht.
    if (limit == 0 || limit > MAX_SAMPLES) limit = MAX_SAMPLES;
    if (count_ && (number != first_ + count_ || count_ >= limit)) flush();
    if (!count_) first_ = number;
    samples_[count_++] = value;
    if (count_ >= limit) flush();
  }

  void flush() {
    if (!count_) return;
    if (size_ == CAPACITY) {
      // Den jüngsten Verlauf erhalten und verworfene Samples (nicht Frames) zählen.
      dropped_ += frames_[tail_].bytes[10];
      pop();
    }
    Frame& frame = frames_[head_];
    frame.bytes[0] = 0xD1;
    frame.bytes[1] = 1;
    put16(frame.bytes + 2, session_);
    put16(frame.bytes + 4, sequence_++);
    put32(frame.bytes + 6, first_);
    frame.bytes[10] = count_;
    frame.bytes[11] = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      put32(frame.bytes + HEADER_BYTES + 4 * i, static_cast<uint32_t>(samples_[i]));
    }
    frame.length = HEADER_BYTES + 4 * count_;
    head_ = (head_ + 1) % CAPACITY;
    ++size_;
    count_ = 0;
  }

  const Frame* front() const { return size_ ? &frames_[tail_] : nullptr; }
  void pop() {
    if (size_) {
      tail_ = (tail_ + 1) % CAPACITY;
      --size_;
    }
  }
  void discard() {
    dropped_ += count_;
    count_ = 0;
    while (front()) {
      dropped_ += front()->bytes[10];
      pop();
    }
  }
  uint32_t dropped() const { return dropped_; }

  static void put16(uint8_t* bytes, uint16_t value) {
    bytes[0] = value;
    bytes[1] = value >> 8;
  }
  static void put32(uint8_t* bytes, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
  }

private:
  Frame frames_[CAPACITY] = {};
  int32_t samples_[MAX_SAMPLES] = {};
  uint32_t first_ = 0;
  uint32_t dropped_ = 0;
  uint16_t session_ = 0;
  uint16_t sequence_ = 0;
  uint8_t head_ = 0, tail_ = 0, size_ = 0, count_ = 0;
};
