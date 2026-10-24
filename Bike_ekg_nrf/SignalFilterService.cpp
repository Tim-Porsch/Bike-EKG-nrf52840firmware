#include "SignalFilterService.h"

#include <limits.h>
#include <math.h>

namespace {
constexpr float PI = 3.14159265358979323846f;

struct Coefficients {
  uint16_t highPassPoleQ15;
  uint16_t highPassFeedQ15;
  uint16_t lowPassStepQ15;
};

bool coefficientsForRate(uint16_t sampleRateSps, uint16_t lowCutoffMilliHz,
                         uint16_t highCutoffMilliHz, Coefficients& coefficients) {
  if (lowCutoffMilliHz == 0 || lowCutoffMilliHz >= highCutoffMilliHz ||
      highCutoffMilliHz >= sampleRateSps * 500) {
    return false;
  }

  if (lowCutoffMilliHz == 500 && highCutoffMilliHz == 40000) {
    switch (sampleRateSps) {
    case 125:
      coefficients = {31955, 32361, 25700};
      return true;
    case 250:
      coefficients = {32359, 32563, 19835};
      return true;
    case 500:
      coefficients = {32563, 32665, 12741};
      return true;
    default:
      return false;
    }
  }

  const float sampleRate = static_cast<float>(sampleRateSps);
  const float lowCutoff = lowCutoffMilliHz / 1000.0f;
  const float highCutoff = highCutoffMilliHz / 1000.0f;
  const float highPassPole = expf(-2.0f * PI * lowCutoff / sampleRate);
  const float highPassFeed = (1.0f + highPassPole) * 0.5f;
  const float lowPassStep = 1.0f - expf(-2.0f * PI * highCutoff / sampleRate);
  coefficients = {
      static_cast<uint16_t>(fminf(32767.0f, roundf(highPassPole * 32768.0f))),
      static_cast<uint16_t>(fminf(32767.0f, roundf(highPassFeed * 32768.0f))),
      static_cast<uint16_t>(fminf(32767.0f, roundf(lowPassStep * 32768.0f))),
  };
  return true;
}
} // namespace

bool SignalFilterService::configure(uint16_t sampleRateSps,
                                    uint16_t lowCutoffMilliHz,
                                    uint16_t highCutoffMilliHz) {
  Coefficients coefficients;
  if (!coefficientsForRate(sampleRateSps, lowCutoffMilliHz,
                           highCutoffMilliHz, coefficients)) {
    configured_ = false;
    reset();
    return false;
  }

  highPassPoleQ15_ = coefficients.highPassPoleQ15;
  highPassFeedQ15_ = coefficients.highPassFeedQ15;
  lowPassStepQ15_ = coefficients.lowPassStepQ15;
  configured_ = true;
  reset();
  return true;
}

void SignalFilterService::reset() {
  previousInput_ = 0;
  highPassState_ = 0;
  lowPassState_ = 0;
  initialized_ = false;
}

int32_t SignalFilterService::process(int32_t sample) {
  // 64-Bit-Zwischenwerte und Sättigung verhindern int32-Überlauf im Filterzustand.
  if (!configured_) {
    return sample;
  }
  if (!initialized_) {
    // Konstante Eingangsvorgeschichte statt eines künstlichen Sprungs von null.
    previousInput_ = sample;
    initialized_ = true;
    return 0;
  }

  const int64_t inputDelta = static_cast<int64_t>(sample) - previousInput_;
  highPassState_ = saturate(
      multiplyQ15(highPassPoleQ15_, highPassState_) +
      multiplyQ15(highPassFeedQ15_, inputDelta));
  previousInput_ = sample;

  lowPassState_ = saturate(
      static_cast<int64_t>(lowPassState_) +
      multiplyQ15(lowPassStepQ15_,
                  static_cast<int64_t>(highPassState_) - lowPassState_));
  return lowPassState_;
}

bool SignalFilterService::isConfigured() const {
  return configured_;
}

int64_t SignalFilterService::multiplyQ15(uint16_t coefficient, int64_t value) {
  const int64_t product = static_cast<int64_t>(coefficient) * value;
  return (product >= 0 ? product + Q15_SCALE / 2 : product - Q15_SCALE / 2) /
         Q15_SCALE;
}

int32_t SignalFilterService::saturate(int64_t value) {
  if (value > INT32_MAX) {
    return INT32_MAX;
  }
  if (value < INT32_MIN) {
    return INT32_MIN;
  }
  return static_cast<int32_t>(value);
}
