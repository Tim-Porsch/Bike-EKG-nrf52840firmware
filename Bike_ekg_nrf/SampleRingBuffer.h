#pragma once

#include <Arduino.h>
#include <nrf_nvic.h>

class SampleRingBuffer {
  // Ein Produzent (ADC-ISR), ein Konsument (loop). Ein Platz bleibt frei, damit
  // head == tail eindeutig leer bedeutet: effektiv sind 1023 Records nutzbar.
public:
  static constexpr uint16_t CAPACITY = 1024;
  static constexpr uint16_t MASK = CAPACITY - 1;

  struct Record {
    uint32_t sampleNumber;
    uint32_t timestampUs;
    int32_t adcValue;
  };
  static_assert(sizeof(Record) == 12, "Unexpected sample record size");

  struct Snapshot {
    uint32_t samplesReceived;
    uint32_t samplesDropped;
    uint32_t samplesProcessed;
    uint32_t sampleReadErrors;
    uint32_t inferredMissedDrdy;
    uint32_t settlingSamplesDiscarded;
    uint16_t fill;
    uint16_t maxFill;
  };

  void reset() {
    uint8_t nested = 0;
    (void)sd_nvic_critical_region_enter(&nested);
    head_ = 0;
    tail_ = 0;
    maxFill_ = 0;
    samplesReceived_ = 0;
    samplesDropped_ = 0;
    samplesProcessed_ = 0;
    sampleReadErrors_ = 0;
    inferredMissedDrdy_ = 0;
    settlingSamplesDiscarded_ = 0;
    (void)sd_nvic_critical_region_exit(nested);
  }

  bool pushFromIsr(const Record& record) {
    const uint16_t head = head_;
    const uint16_t next = (head + 1) & MASK;
    if (next == tail_) {
      ++samplesDropped_;
      return false;
    }

    records_[head] = record;
    // Inhalt vor dem neuen Schreibindex veröffentlichen, nicht umgekehrt.
    __DMB();
    head_ = next;

    const uint16_t fill = (next - tail_) & MASK;
    if (fill > maxFill_) {
      maxFill_ = fill;
    }
    return true;
  }

  void recordReceivedFromIsr() {
    ++samplesReceived_;
  }

  void recordReadErrorFromIsr() {
    ++sampleReadErrors_;
    ++samplesDropped_;
  }

  void recordInferredMissedDrdyFromIsr(uint32_t count) {
    inferredMissedDrdy_ += count;
    samplesDropped_ += count;
  }

  void recordSettlingDiscardFromIsr() {
    ++settlingSamplesDiscarded_;
  }

  bool pop(Record& record) {
    uint8_t nested = 0;
    (void)sd_nvic_critical_region_enter(&nested);

    const uint16_t tail = tail_;
    if (tail == head_) {
      (void)sd_nvic_critical_region_exit(nested);
      return false;
    }

    __DMB();
    record = records_[tail];
    tail_ = (tail + 1) & MASK;
    ++samplesProcessed_;
    (void)sd_nvic_critical_region_exit(nested);
    return true;
  }

  Snapshot snapshot() const {
    // Zusammengehörige Statistik atomar lesen, während die ISR weiterzählen könnte.
    uint8_t nested = 0;
    (void)sd_nvic_critical_region_enter(&nested);
    const Snapshot result{
        samplesReceived_,
        samplesDropped_,
        samplesProcessed_,
        sampleReadErrors_,
        inferredMissedDrdy_,
        settlingSamplesDiscarded_,
        static_cast<uint16_t>((head_ - tail_) & MASK),
        maxFill_,
    };
    (void)sd_nvic_critical_region_exit(nested);
    return result;
  }

private:
  Record records_[CAPACITY];
  volatile uint16_t head_ = 0;
  volatile uint16_t tail_ = 0;
  volatile uint16_t maxFill_ = 0;
  volatile uint32_t samplesReceived_ = 0;
  volatile uint32_t samplesDropped_ = 0;
  volatile uint32_t samplesProcessed_ = 0;
  volatile uint32_t sampleReadErrors_ = 0;
  volatile uint32_t inferredMissedDrdy_ = 0;
  volatile uint32_t settlingSamplesDiscarded_ = 0;
};
