#include "AdsService.h"

#include <nrf_nvic.h>
#include <nrf_soc.h>

namespace {
constexpr uint32_t DRDY_GPIO = 39; // P1.07
constexpr uint8_t EGU_TRIGGER = 0;
constexpr uint8_t EGU_PPI_CHANNEL = 0;
constexpr uint8_t TIMESTAMP_PPI_CHANNEL = 1;
constexpr uint32_t PPI_CHANNEL_MASK = (1UL << EGU_PPI_CHANNEL) |
                                      (1UL << TIMESTAMP_PPI_CHANNEL);
AdsService* interruptService = nullptr;
bool ppiReservedByAds = false;
} // namespace

extern "C" void SWI0_EGU0_IRQHandler(void) {
  if (NRF_EGU0->EVENTS_TRIGGERED[EGU_TRIGGER] == 0) {
    return;
  }

  NRF_EGU0->EVENTS_TRIGGERED[EGU_TRIGGER] = 0;
  if (interruptService != nullptr) {
    interruptService->handleDrdyInterrupt();
  }
  __DSB();
}

AdsService::AdsService(TimeService& timeService) : timeService_(timeService) {}

bool AdsService::start(const MeasurementSettings& settings) {
  if (running_) {
    lastError_ = "ADS is already running";
    return false;
  }
  if (!timeService_.isClockRunning()) {
    lastError_ = "timestamp timer is not running";
    return false;
  }
  if (!supportsSampleRate(settings)) {
    lastError_ = "rates above 500 SPS are not supported by the current SPI path";
    return false;
  }

  const bool testSignal = settings.input == Ads1291::InputMux::TestSignal;
  const bool configured =
      ads_.begin() && ads_.setSampleRate(settings.sampleRate) &&
      ads_.configureTestSignal(testSignal) &&
      ads_.configureChannel(settings.gain, settings.input) &&
      ads_.configureRld(settings.rldEnabled) &&
      ads_.calibrateOffset();
  if (!configured) {
    lastError_ = "ADS configuration failed";
    return false;
  }

  ringBuffer_.reset();
  nextSampleNumber_ = 0;
  settlingSamplesToDiscard_ = FILTER_SAMPLES_TO_DISCARD;
  samplePeriodUs_ = samplePeriodUs(settings);
  hasPreviousDrdy_ = false;
  measurementStartedUs_ = timeService_.bootTimeUs();
  if (!startDrdyInterrupt()) {
    lastError_ = "DRDY interrupt setup failed";
    return false;
  }

  running_ = true;
  if (!ads_.startConversions()) {
    running_ = false;
    stopDrdyInterrupt();
    lastError_ = "ADS start command failed";
    return false;
  }

  lastError_ = "none";
  return true;
}

void AdsService::stop() {
  stopDrdyInterrupt();
  running_ = false;
  ads_.stopConversions();
}

bool AdsService::isRunning() const {
  return running_;
}

const char* AdsService::lastError() const {
  return lastError_;
}

bool AdsService::popSample(SampleRingBuffer::Record& record) {
  return ringBuffer_.pop(record);
}

SampleRingBuffer::Snapshot AdsService::snapshot() const {
  return ringBuffer_.snapshot();
}

uint32_t AdsService::elapsedUs() const {
  return timeService_.bootTimeUs() - measurementStartedUs_;
}

void AdsService::handleDrdyInterrupt() {
  // Erfassung kurz halten: Zeitstempel wurde bereits an der DRDY-Flanke gelatcht.
  if (gpioteChannel_ >= 0) {
    NRF_GPIOTE->EVENTS_IN[gpioteChannel_] = 0;
  }

  if (!running_) {
    return;
  }

  const uint32_t drdyUs = timeService_.bootTimeUsFromIsr();
  if (hasPreviousDrdy_) {
    const uint32_t intervalUs = drdyUs - previousDrdyUs_;
    if (intervalUs > samplePeriodUs_ + samplePeriodUs_ / 2) {
      // Zeitabstände liefern eine Schätzung verpasster Flanken, keine Nachmessung.
      // Nummern auslassen, damit Empfänger fehlende Werte nicht als stetig zeichnen.
      const uint32_t inferredMisses =
          (intervalUs + samplePeriodUs_ / 2) / samplePeriodUs_ - 1;
      ringBuffer_.recordInferredMissedDrdyFromIsr(inferredMisses);
      nextSampleNumber_ += inferredMisses;
    }
  }
  previousDrdyUs_ = drdyUs;
  hasPreviousDrdy_ = true;

  Ads1291::Sample sample;
  const uint32_t timestampUs = drdyUs - measurementStartedUs_;
  if (!ads_.readSample(sample)) {
    ringBuffer_.recordReadErrorFromIsr();
    ++nextSampleNumber_;
    return;
  }

  ringBuffer_.recordReceivedFromIsr();

  // Einschwingen nach dem Start wird separat gezählt, nicht als Nutzsample geloggt.
  if (settlingSamplesToDiscard_ > 0) {
    --settlingSamplesToDiscard_;
    ringBuffer_.recordSettlingDiscardFromIsr();
    return;
  }

  ringBuffer_.pushFromIsr(
      {nextSampleNumber_++, timestampUs, sample.channel1});
}

bool AdsService::startDrdyInterrupt() {
  // GPIOTE verteilt dieselbe Flanke per PPI auf Timer-Capture und EGU-Interrupt.
  // So hängt die Messzeit nicht von der Verzögerung bis zum ISR-Eintritt ab.
  if (interruptService != nullptr) {
    return false;
  }

  int8_t gpioteChannel = -1;
  for (uint8_t channel = 0; channel < 8; channel++) {
    if (NRF_GPIOTE->CONFIG[channel] == 0) {
      gpioteChannel = channel;
      break;
    }
  }
  if (gpioteChannel < 0) {
    return false;
  }

  if (!ppiReservedByAds &&
      ((NRF_PPI->CHEN & PPI_CHANNEL_MASK) != 0 ||
       NRF_PPI->CH[EGU_PPI_CHANNEL].EEP != 0 ||
       NRF_PPI->CH[EGU_PPI_CHANNEL].TEP != 0 ||
       NRF_PPI->CH[TIMESTAMP_PPI_CHANNEL].EEP != 0 ||
       NRF_PPI->CH[TIMESTAMP_PPI_CHANNEL].TEP != 0)) {
    return false;
  }

  const uint32_t gpioteConfig =
      ((DRDY_GPIO << GPIOTE_CONFIG_PSEL_Pos) &
       (GPIOTE_CONFIG_PSEL_Msk | GPIOTE_CONFIG_PORT_Msk)) |
      ((GPIOTE_CONFIG_POLARITY_HiToLo << GPIOTE_CONFIG_POLARITY_Pos) &
       GPIOTE_CONFIG_POLARITY_Msk) |
      ((GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos) &
       GPIOTE_CONFIG_MODE_Msk);

  NRF_GPIOTE->EVENTS_IN[gpioteChannel] = 0;
  NRF_GPIOTE->CONFIG[gpioteChannel] = gpioteConfig;
  NRF_EGU0->EVENTS_TRIGGERED[EGU_TRIGGER] = 0;
  NRF_EGU0->INTENSET = EGU_INTENSET_TRIGGERED0_Msk;

  gpioteChannel_ = gpioteChannel;
  interruptService = this;

  if (sd_nvic_SetPriority(SWI0_EGU0_IRQn, 3) != NRF_SUCCESS ||
      sd_nvic_ClearPendingIRQ(SWI0_EGU0_IRQn) != NRF_SUCCESS ||
      sd_ppi_channel_assign(
          EGU_PPI_CHANNEL, &NRF_GPIOTE->EVENTS_IN[gpioteChannel_],
          &NRF_EGU0->TASKS_TRIGGER[EGU_TRIGGER]) != NRF_SUCCESS) {
    stopDrdyInterrupt();
    return false;
  }

  ppiReservedByAds = true;
  if (sd_ppi_channel_assign(
          TIMESTAMP_PPI_CHANNEL, &NRF_GPIOTE->EVENTS_IN[gpioteChannel_],
          &NRF_TIMER4->TASKS_CAPTURE[0]) != NRF_SUCCESS) {
    stopDrdyInterrupt();
    return false;
  }

  if (sd_nvic_EnableIRQ(SWI0_EGU0_IRQn) != NRF_SUCCESS ||
      sd_ppi_channel_enable_set(PPI_CHANNEL_MASK) != NRF_SUCCESS) {
    stopDrdyInterrupt();
    return false;
  }

  return true;
}

void AdsService::stopDrdyInterrupt() {
  if (gpioteChannel_ < 0) {
    return;
  }

  (void)sd_ppi_channel_enable_clr(PPI_CHANNEL_MASK);
  (void)sd_nvic_DisableIRQ(SWI0_EGU0_IRQn);
  NRF_EGU0->INTENCLR = EGU_INTENCLR_TRIGGERED0_Msk;
  if (gpioteChannel_ >= 0) {
    NRF_GPIOTE->CONFIG[gpioteChannel_] = 0;
    NRF_GPIOTE->EVENTS_IN[gpioteChannel_] = 0;
  }
  NRF_EGU0->EVENTS_TRIGGERED[EGU_TRIGGER] = 0;
  (void)sd_nvic_ClearPendingIRQ(SWI0_EGU0_IRQn);
  __DSB();

  interruptService = nullptr;
  gpioteChannel_ = -1;
}

bool AdsService::supportsSampleRate(const MeasurementSettings& settings) const {
  return settings.sampleRate == Ads1291::SampleRate::Sps125 ||
         settings.sampleRate == Ads1291::SampleRate::Sps250 ||
         settings.sampleRate == Ads1291::SampleRate::Sps500;
}

uint32_t AdsService::samplePeriodUs(const MeasurementSettings& settings) const {
  switch (settings.sampleRate) {
  case Ads1291::SampleRate::Sps125:
    return 8000;
  case Ads1291::SampleRate::Sps250:
    return 4000;
  case Ads1291::SampleRate::Sps500:
    return 2000;
  default:
    return 0;
  }
}
