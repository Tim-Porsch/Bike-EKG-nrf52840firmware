#include "../SignalFilterService.h"

// Festkommafilter mit synthetischen Signalen prüfen: Konfiguration, DC-Einschwingen
// und relative Durchlass-/Sperrdämpfung, nicht die analoge Messkette.

#include <assert.h>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace {
constexpr double PI = 3.14159265358979323846;

double sineGain(uint16_t sampleRateSps, double frequencyHz,
                uint16_t lowCutoffMilliHz = 500,
                uint16_t highCutoffMilliHz = 40000) {
  constexpr int32_t AMPLITUDE = 1000000;
  SignalFilterService filter;
  assert(filter.configure(sampleRateSps, lowCutoffMilliHz,
                          highCutoffMilliHz));

  const uint32_t warmupSamples = sampleRateSps * 10;
  const uint32_t measuredSamples = sampleRateSps * 10;
  double energy = 0.0;
  for (uint32_t index = 0; index < warmupSamples + measuredSamples; index++) {
    const double phase = 2.0 * PI * frequencyHz * index / sampleRateSps;
    const int32_t input = static_cast<int32_t>(AMPLITUDE * std::sin(phase));
    const int32_t output = filter.process(input);
    if (index < warmupSamples) {
      continue;
    }
    energy += static_cast<double>(output) * output;
    if (index + 1 == warmupSamples + measuredSamples) {
      const double outputRms = std::sqrt(energy / measuredSamples);
      return outputRms / (AMPLITUDE / std::sqrt(2.0));
    }
  }

  return 0.0;
}

void verifyDcRemoval(uint16_t sampleRateSps) {
  SignalFilterService filter;
  assert(filter.configure(sampleRateSps));
  for (uint32_t index = 0; index < sampleRateSps * 5; index++) {
    filter.process(1000000);
  }
  assert(std::abs(filter.process(1000000)) < 100);
}
} // namespace

int main() {
  SignalFilterService filter;
  assert(!filter.configure(100));
  assert(filter.configure(125));
  assert(filter.configure(250));
  assert(filter.configure(500));
  assert(!filter.configure(250, 40000, 5000));
  assert(!filter.configure(125, 5000, 65000));
  assert(filter.configure(250, 5000, 40000));

  for (const uint16_t sampleRateSps : {125, 250, 500}) {
    verifyDcRemoval(sampleRateSps);
    const double gainAtOneHz = sineGain(sampleRateSps, 1.0);
    const double gainAtFiftyHz = sineGain(sampleRateSps, 50.0);
    assert(gainAtOneHz > 0.80 && gainAtOneHz < 1.10);
    assert(gainAtFiftyHz < gainAtOneHz * 0.82);
  }

  const double gainAtOneHz = sineGain(250, 1.0, 5000, 40000);
  const double gainAtTenHz = sineGain(250, 10.0, 5000, 40000);
  assert(gainAtOneHz < gainAtTenHz * 0.35);
  assert(gainAtTenHz > 0.80);
}
