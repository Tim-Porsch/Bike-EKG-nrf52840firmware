#include "SettingsService.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {
constexpr char SETTINGS_FILE[] = "/bike_ekg_settings.bin";
constexpr char SETTINGS_TEMP_FILE[] = "/bike_ekg_settings.tmp";
constexpr uint32_t SETTINGS_MAGIC = 0x53474B42;
constexpr uint8_t SETTINGS_VERSION = 3;
constexpr uint32_t CHECKSUM_INITIAL = 2166136261UL;
constexpr uint32_t CHECKSUM_PRIME = 16777619UL;

struct __attribute__((packed)) StoredSettingsV1 {
  uint32_t magic;
  uint8_t version;
  uint16_t sampleRateSps;
  uint8_t gainMultiplier;
  uint8_t inputMux;
  uint32_t testDurationSeconds;
  uint32_t checksum;
};

struct __attribute__((packed)) StoredSettingsV2 {
  uint32_t magic;
  uint8_t version;
  uint16_t sampleRateSps;
  uint8_t gainMultiplier;
  uint8_t inputMux;
  uint32_t testDurationSeconds;
  uint16_t filterLowCutoffMilliHz;
  uint16_t filterHighCutoffMilliHz;
  uint32_t checksum;
};

struct __attribute__((packed)) StoredSettings {
  uint32_t magic;
  uint8_t version;
  uint16_t sampleRateSps;
  uint8_t gainMultiplier;
  uint8_t inputMux;
  uint32_t testDurationSeconds;
  uint16_t filterLowCutoffMilliHz;
  uint16_t filterHighCutoffMilliHz;
  uint8_t rldEnabled;
  uint32_t checksum;
};

static_assert(sizeof(StoredSettingsV1) == 17, "Settings V1 layout changed");
static_assert(sizeof(StoredSettingsV2) == 21, "Settings V2 layout changed");
static_assert(sizeof(StoredSettings) == 22, "Settings V3 layout changed");

template <typename T>
uint32_t checksum(const T& stored) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&stored);
  uint32_t value = CHECKSUM_INITIAL;
  for (size_t index = 0; index < sizeof(stored) - sizeof(stored.checksum);
       index++) {
    value = (value ^ bytes[index]) * CHECKSUM_PRIME;
  }
  return value;
}
} // namespace

bool SettingsService::begin() {
  // V1/V2 remain readable; their previously implicit RLD-off state is preserved.
  settings_ = MeasurementSettings{};
  if (!InternalFS.begin()) {
    return false;
  }

  File file(InternalFS);
  if (!file.open(SETTINGS_FILE, FILE_O_READ)) {
    return true;
  }

  StoredSettings stored = {};
  const int bytesRead = file.read(&stored, sizeof(stored));
  const bool hasExtraBytes = file.available() > 0;
  file.close();
  if (hasExtraBytes) return true;
  if (bytesRead == sizeof(StoredSettingsV2)) {
    StoredSettingsV2 legacy = {};
    memcpy(&legacy, &stored, sizeof(legacy));
    if (legacy.magic != SETTINGS_MAGIC || legacy.version != 2 ||
        legacy.checksum != checksum(legacy)) return true;
    stored.filterLowCutoffMilliHz = legacy.filterLowCutoffMilliHz;
    stored.filterHighCutoffMilliHz = legacy.filterHighCutoffMilliHz;
    stored.rldEnabled = 0;
  } else if (bytesRead == sizeof(StoredSettingsV1)) {
    StoredSettingsV1 legacy = {};
    memcpy(&legacy, &stored, sizeof(legacy));
    if (legacy.magic != SETTINGS_MAGIC || legacy.version != 1 ||
        legacy.checksum != checksum(legacy)) return true;
    stored.filterLowCutoffMilliHz = settings_.filterLowCutoffMilliHz;
    stored.filterHighCutoffMilliHz = settings_.filterHighCutoffMilliHz;
    stored.rldEnabled = 0;
  } else if (bytesRead != sizeof(stored) || stored.magic != SETTINGS_MAGIC ||
             stored.version != SETTINGS_VERSION || stored.checksum != checksum(stored)) {
    return true;
  }
  if (stored.rldEnabled > 1 || !setSampleRate(stored.sampleRateSps) ||
      !setGain(stored.gainMultiplier) || !setTestDurationSeconds(stored.testDurationSeconds) ||
      !setFilterBandMilliHz(stored.filterLowCutoffMilliHz, stored.filterHighCutoffMilliHz)) {
    settings_ = MeasurementSettings{};
    return true;
  }

  switch (static_cast<Ads1291::InputMux>(stored.inputMux)) {
  case Ads1291::InputMux::NormalElectrode:
  case Ads1291::InputMux::InputShorted:
  case Ads1291::InputMux::TestSignal:
    settings_.input = static_cast<Ads1291::InputMux>(stored.inputMux);
    settings_.rldEnabled = stored.rldEnabled != 0;
    break;
  default:
    settings_ = MeasurementSettings{};
    break;
  }
  return true;
}

bool SettingsService::save() const {
  // Erst vollständig in eine temporäre Datei schreiben, dann per rename ersetzen.
  // Prüfsumme schützt das Speicherformat, nicht vor absichtlicher Manipulation.
  StoredSettings stored = {
      SETTINGS_MAGIC,
      SETTINGS_VERSION,
      sampleRateSps(),
      gainMultiplier(),
      static_cast<uint8_t>(settings_.input),
      settings_.testDurationSeconds,
      settings_.filterLowCutoffMilliHz,
      settings_.filterHighCutoffMilliHz,
      static_cast<uint8_t>(settings_.rldEnabled),
      0,
  };
  stored.checksum = checksum(stored);

  File file(InternalFS);
  if (!file.open(SETTINGS_TEMP_FILE, FILE_O_WRITE)) {
    return false;
  }
  const size_t bytesWritten =
      file.write(reinterpret_cast<const uint8_t*>(&stored), sizeof(stored));
  file.close();
  if (bytesWritten != sizeof(stored)) {
    InternalFS.remove(SETTINGS_TEMP_FILE);
    return false;
  }
  if (!InternalFS.rename(SETTINGS_TEMP_FILE, SETTINGS_FILE)) {
    InternalFS.remove(SETTINGS_TEMP_FILE);
    return false;
  }
  return true;
}

bool SettingsService::reset() {
  const MeasurementSettings previous = settings_;
  settings_ = MeasurementSettings{};
  if (save()) return true;
  settings_ = previous;
  return false;
}

const MeasurementSettings& SettingsService::current() const {
  return settings_;
}

bool SettingsService::setSampleRate(uint32_t samplesPerSecond) {
  if (settings_.filterHighCutoffMilliHz >= samplesPerSecond * 500) {
    return false;
  }
  switch (samplesPerSecond) {
  case 125:
    settings_.sampleRate = Ads1291::SampleRate::Sps125;
    return true;
  case 250:
    settings_.sampleRate = Ads1291::SampleRate::Sps250;
    return true;
  case 500:
    settings_.sampleRate = Ads1291::SampleRate::Sps500;
    return true;
  default:
    return false;
  }
}

bool SettingsService::setGain(uint32_t gain) {
  switch (gain) {
  case 1:
    settings_.gain = Ads1291::Gain::X1;
    return true;
  case 2:
    settings_.gain = Ads1291::Gain::X2;
    return true;
  case 3:
    settings_.gain = Ads1291::Gain::X3;
    return true;
  case 4:
    settings_.gain = Ads1291::Gain::X4;
    return true;
  case 6:
    settings_.gain = Ads1291::Gain::X6;
    return true;
  case 8:
    settings_.gain = Ads1291::Gain::X8;
    return true;
  case 12:
    settings_.gain = Ads1291::Gain::X12;
    return true;
  default:
    return false;
  }
}

bool SettingsService::setInput(const char* input) {
  if (strcmp(input, "normal") == 0) {
    settings_.input = Ads1291::InputMux::NormalElectrode;
    return true;
  }
  if (strcmp(input, "short") == 0) {
    settings_.input = Ads1291::InputMux::InputShorted;
    return true;
  }
  if (strcmp(input, "test") == 0) {
    settings_.input = Ads1291::InputMux::TestSignal;
    return true;
  }

  return false;
}

void SettingsService::setRldEnabled(bool enabled) {
  settings_.rldEnabled = enabled;
}

bool SettingsService::setTestDurationSeconds(uint32_t seconds) {
  if (seconds == 0 || seconds > MAX_TEST_DURATION_SECONDS) {
    return false;
  }

  settings_.testDurationSeconds = seconds;
  return true;
}

bool SettingsService::setFilterBandMilliHz(uint32_t lowCutoffMilliHz,
                                            uint32_t highCutoffMilliHz) {
  if (lowCutoffMilliHz == 0 || lowCutoffMilliHz >= highCutoffMilliHz ||
      highCutoffMilliHz > UINT16_MAX ||
      highCutoffMilliHz >= sampleRateSps() * 500) {
    return false;
  }
  settings_.filterLowCutoffMilliHz = static_cast<uint16_t>(lowCutoffMilliHz);
  settings_.filterHighCutoffMilliHz = static_cast<uint16_t>(highCutoffMilliHz);
  return true;
}

uint16_t SettingsService::sampleRateSps() const {
  switch (settings_.sampleRate) {
  case Ads1291::SampleRate::Sps125:
    return 125;
  case Ads1291::SampleRate::Sps250:
    return 250;
  case Ads1291::SampleRate::Sps500:
    return 500;
  case Ads1291::SampleRate::Sps1000:
    return 1000;
  case Ads1291::SampleRate::Sps2000:
    return 2000;
  case Ads1291::SampleRate::Sps4000:
    return 4000;
  case Ads1291::SampleRate::Sps8000:
    return 8000;
  }

  return 0;
}

uint8_t SettingsService::gainMultiplier() const {
  switch (settings_.gain) {
  case Ads1291::Gain::X1:
    return 1;
  case Ads1291::Gain::X2:
    return 2;
  case Ads1291::Gain::X3:
    return 3;
  case Ads1291::Gain::X4:
    return 4;
  case Ads1291::Gain::X6:
    return 6;
  case Ads1291::Gain::X8:
    return 8;
  case Ads1291::Gain::X12:
    return 12;
  }

  return 0;
}

const char* SettingsService::inputName() const {
  switch (settings_.input) {
  case Ads1291::InputMux::NormalElectrode:
    return "normal";
  case Ads1291::InputMux::InputShorted:
    return "short";
  case Ads1291::InputMux::TestSignal:
    return "test";
  default:
    return "unsupported";
  }
}

uint16_t SettingsService::filterLowCutoffMilliHz() const {
  return settings_.filterLowCutoffMilliHz;
}

uint16_t SettingsService::filterHighCutoffMilliHz() const {
  return settings_.filterHighCutoffMilliHz;
}
