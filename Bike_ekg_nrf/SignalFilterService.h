#pragma once

// Kausaler Hochpass + Tiefpass erster Ordnung in Q15-Festkommaarithmetik.
// Nur Signalpfade für Anzeige/Streaming filtern; SD-Rohwerte bleiben unverändert.

#include <stdint.h>

// Fixed-point first-order bandpass for signed ADC counts.
class SignalFilterService {
public:
  bool configure(uint16_t sampleRateSps, uint16_t lowCutoffMilliHz = 500,
                 uint16_t highCutoffMilliHz = 40000);
  void reset();
  int32_t process(int32_t sample);
  bool isConfigured() const;

private:
  static constexpr int64_t Q15_SCALE = 32768;

  uint16_t highPassPoleQ15_ = 0;
  uint16_t highPassFeedQ15_ = 0;
  uint16_t lowPassStepQ15_ = 0;
  int32_t previousInput_ = 0;
  int32_t highPassState_ = 0;
  int32_t lowPassState_ = 0;
  bool configured_ = false;
  bool initialized_ = false;

  static int64_t multiplyQ15(uint16_t coefficient, int64_t value);
  static int32_t saturate(int64_t value);
};
