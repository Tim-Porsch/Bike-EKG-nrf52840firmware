#pragma once

// Bindet ADC-Treiber, hardwareerfasste Zeitstempel und ISR-Ringpuffer zusammen.
// popSample() wird in loop() konsumiert; die ISR führt keine SD-/BLE-Arbeit aus.

#include "Ads1291.h"
#include "SampleRingBuffer.h"
#include "SettingsService.h"
#include "TimeService.h"

class AdsService {
public:
  explicit AdsService(TimeService& timeService);

  bool start(const MeasurementSettings& settings);
  void stop();
  bool isRunning() const;
  const char* lastError() const;

  bool popSample(SampleRingBuffer::Record& record);
  SampleRingBuffer::Snapshot snapshot() const;
  uint32_t elapsedUs() const;

  void handleDrdyInterrupt();

private:
  static constexpr uint8_t FILTER_SAMPLES_TO_DISCARD = 4;

  Ads1291 ads_;
  SampleRingBuffer ringBuffer_;
  TimeService& timeService_;
  volatile bool running_ = false;
  int8_t gpioteChannel_ = -1;
  uint32_t measurementStartedUs_ = 0;
  uint32_t previousDrdyUs_ = 0;
  uint32_t samplePeriodUs_ = 0;
  uint32_t nextSampleNumber_ = 0;
  uint8_t settlingSamplesToDiscard_ = 0;
  bool hasPreviousDrdy_ = false;
  const char* lastError_ = "not started";

  bool startDrdyInterrupt();
  void stopDrdyInterrupt();
  bool supportsSampleRate(const MeasurementSettings& settings) const;
  uint32_t samplePeriodUs(const MeasurementSettings& settings) const;
};
