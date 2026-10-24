#pragma once

// Messparameter und zentrale Gültigkeitsgrenzen; Filterfrequenzen in Millihertz.
// RLD wird ab Speicherformat V3 ebenfalls dauerhaft gespeichert.

#include "Ads1291.h"

struct MeasurementSettings {
  Ads1291::SampleRate sampleRate = Ads1291::SampleRate::Sps500;
  Ads1291::Gain gain = Ads1291::Gain::X6;
  Ads1291::InputMux input = Ads1291::InputMux::NormalElectrode;
  bool rldEnabled = true; // Ausschließlich elektrisch isolierter Prüfaufbau.
  uint32_t testDurationSeconds = 300;
  uint16_t filterLowCutoffMilliHz = 500;
  uint16_t filterHighCutoffMilliHz = 40000;
};

class SettingsService {
public:
  static constexpr uint32_t MAX_TEST_DURATION_SECONDS = 4200;

  bool begin();
  bool save() const;
  bool reset();
  const MeasurementSettings& current() const;
  bool setSampleRate(uint32_t samplesPerSecond);
  bool setGain(uint32_t gain);
  bool setInput(const char* input);
  void setRldEnabled(bool enabled);
  bool setTestDurationSeconds(uint32_t seconds);
  bool setFilterBandMilliHz(uint32_t lowCutoffMilliHz,
                            uint32_t highCutoffMilliHz);

  uint16_t sampleRateSps() const;
  uint8_t gainMultiplier() const;
  const char* inputName() const;
  uint16_t filterLowCutoffMilliHz() const;
  uint16_t filterHighCutoffMilliHz() const;

private:
  MeasurementSettings settings_;
};
