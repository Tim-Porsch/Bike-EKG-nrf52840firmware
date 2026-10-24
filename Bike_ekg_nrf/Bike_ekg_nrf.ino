#include <Adafruit_TinyUSB.h>
// Zentrale Ablaufsteuerung: genau ein aktiver Mess-/Übertragungsmodus.
// ADC-ISR produziert Rohsamples; loop() verteilt sie an SD, USB oder BLE.
// Protokolltexte sind zugleich Schnittstelle zu den Hostanwendungen.
#include <bluefruit.h>

#include "AdsService.h"
#include "ConsoleService.h"
#include "DisplayBleService.h"
#include "LogWriter.h"
#include "PreviewBleService.h"
#include "SettingsService.h"
#include "SignalFilterService.h"
#include "SdService.h"
#include "SdFileBleService.h"
#include "TimeService.h"

constexpr char FIRMWARE_VERSION[] = "0.12.5";
constexpr uint32_t LOGGING_PROGRESS_INTERVAL_US = 10000000UL;
constexpr uint32_t BLE_PROGRESS_INTERVAL_US = 30000000UL;
constexpr uint16_t DISPLAY_SAMPLE_RATE_SPS = 250;
constexpr uint16_t DISPLAY_WARMUP_SAMPLES = 500;

enum class AppState : uint8_t {
  // Files sperrt konkurrierende Messungen auch während Metadaten vorbereitet werden.
  Init,
  Idle,
  Logging,
  Streaming,
  Preview,
  Files,
  Display,
};

BLEUart bleUart;
ConsoleService console(bleUart);
SettingsService settings;
TimeService timeService;
AdsService adsService(timeService);
SdService sdService;
SdFileBleService sdFileBleService(sdService);
LogWriter logWriter(sdService);
SignalFilterService signalFilter;
DisplayBleService displayBleService;
PreviewBleService previewBleService;
AppState appState = AppState::Init;
SampleRingBuffer::Record lastSample = {};
bool hasLastSample = false;
uint32_t lastLoggingProgressUs = 0;
uint32_t lastBleProgressUs = 0;
bool streamFilterEnabled = false;
bool hasExpectedFilteredSample = false;
uint32_t expectedFilteredSampleNumber = 0;
bool hasExpectedDisplaySample = false;
uint32_t expectedDisplaySampleNumber = 0;
uint16_t displayWarmupSamplesRemaining = 0;
volatile bool advertisingRestartPending = false;
volatile bool previewDisconnectPending = false;
uint32_t lastAdvertisingRestartAttemptMs = 0;

const char* stateName(AppState state) {
  switch (state) {
  case AppState::Init:
    return "init";
  case AppState::Idle:
    return "idle";
  case AppState::Logging:
    return "logging";
  case AppState::Streaming:
    return "streaming";
  case AppState::Display:
    return "display";
  case AppState::Preview:
    return "preview";
  case AppState::Files:
    return "files";
  }

  return "unknown";
}

bool configureAdvertising(BLEService& service, const char* deviceName,
                          bool disconnectCurrentCentral) {
  Bluefruit.Advertising.restartOnDisconnect(false);
  if (Bluefruit.Advertising.isRunning() && !Bluefruit.Advertising.stop()) {
    return false;
  }
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.setName(deviceName);
  if (!Bluefruit.Advertising.addFlags(
          BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) ||
      !Bluefruit.Advertising.addTxPower() ||
      !Bluefruit.Advertising.addService(service) ||
      !Bluefruit.ScanResponse.addName()) {
    return false;
  }
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);

  if (disconnectCurrentCentral && Bluefruit.Periph.connected()) {
    const uint16_t connectionHandle = Bluefruit.connHandle();
    return connectionHandle != BLE_CONN_HANDLE_INVALID &&
           Bluefruit.disconnect(connectionHandle);
  }
  return Bluefruit.Advertising.start(0);
}

void restartAdvertisingAfterDisconnect(uint16_t connectionHandle,
                                       uint8_t reason) {
  (void)connectionHandle;
  (void)reason;
  console.disconnected();
  previewDisconnectPending = true;
  sdFileBleService.disconnected();
  if (!Bluefruit.Advertising.isRunning() &&
      !Bluefruit.Advertising.start(0)) {
    advertisingRestartPending = true;
  }
}

void updateAdvertisingRestart() {
  if (!advertisingRestartPending || Bluefruit.Periph.connected() ||
      Bluefruit.Advertising.isRunning() ||
      millis() - lastAdvertisingRestartAttemptMs < 1000) {
    return;
  }

  lastAdvertisingRestartAttemptMs = millis();
  if (Bluefruit.Advertising.start(0)) {
    advertisingRestartPending = false;
  }
}

void startBleAdvertising() {
  (void)configureAdvertising(bleUart, "Bike-EKG", false);
}

void beginBle() {
  Bluefruit.autoConnLed(false);
  // Configure SoftDevice notification buffers before Bluefruit.begin().
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(4);
  Bluefruit.Periph.setConnInterval(6, 12); // Prefer 7.5-15 ms for display data.
  Bluefruit.Periph.setDisconnectCallback(restartAdvertisingAfterDisconnect);
  bleUart.begin();
  displayBleService.begin();
  previewBleService.begin();
  sdFileBleService.begin();
  startBleAdvertising();
}

void sendHelp(CommandSource source) {
  console.reply(source,
                "commands:\r\n"
                "  help\r\n"
                "  state\r\n"
                "  status\r\n"
                "  time <unix_seconds>\r\n"
                "  config\r\n"
                "  gain <1|2|3|4|6|8|12>\r\n"
                "  rate <125|250|500>\r\n"
                "  input <normal|short|test>\r\n"
                 "  rld <on|off>\r\n"
                 "  test_duration <seconds>\r\n"
                 "  filter <on|off>\r\n"
                 "  filter band <low_hz> <high_hz>\r\n"
                "  config reset\r\n"
                "  display start\r\n"
                "  start\r\n"
                "  stop\r\n"
                "  stream <start|stop>\r\n");
}

bool drainSamples() {
  SampleRingBuffer::Record sample;
  while (adsService.popSample(sample)) {
    lastSample = sample;
    hasLastSample = true;
    if (!logWriter.append(sample)) {
      return false;
    }
  }
  return true;
}

void sendLoggingProgressIfDue() {
  const uint32_t elapsedUs = adsService.elapsedUs();
  if (elapsedUs - lastLoggingProgressUs < LOGGING_PROGRESS_INTERVAL_US) {
    return;
  }

  const uint32_t durationUs =
      settings.current().testDurationSeconds * 1000000UL;
  if (elapsedUs >= durationUs) {
    return;
  }

  char message[80];
  const uint32_t elapsedSeconds = elapsedUs / 1000000UL;
  const uint32_t remainingSeconds =
      (durationUs - elapsedUs + 999999UL) / 1000000UL;
  const int length = snprintf(message, sizeof(message),
                              "logging: %lu/%lu s, remaining: ~%lu s\r\n",
                              static_cast<unsigned long>(elapsedSeconds),
                              static_cast<unsigned long>(durationUs / 1000000UL),
                              static_cast<unsigned long>(remainingSeconds));
  if (length <= 0 || static_cast<size_t>(length) > sizeof(message) - 1 ||
      Serial.availableForWrite() < length) {
    return;
  }

  if (Serial.write(reinterpret_cast<const uint8_t*>(message), length) ==
      static_cast<size_t>(length)) {
    lastLoggingProgressUs = elapsedUs;
  }
}

void sendBleProgressIfDue() {
  const uint32_t elapsedUs = adsService.elapsedUs();
  if (elapsedUs - lastBleProgressUs < BLE_PROGRESS_INTERVAL_US) {
    return;
  }

  const uint32_t durationUs =
      settings.current().testDurationSeconds * 1000000UL;
  if (elapsedUs >= durationUs) {
    return;
  }

  char message[64];
  const uint32_t elapsedSeconds = elapsedUs / 1000000UL;
  const uint32_t remainingSeconds =
      (durationUs - elapsedUs + 999999UL) / 1000000UL;
  snprintf(message, sizeof(message), "progress: %lu/%lu s, ~%lu s left\r\n",
           static_cast<unsigned long>(elapsedSeconds),
           static_cast<unsigned long>(durationUs / 1000000UL),
           static_cast<unsigned long>(remainingSeconds));
  console.reply(CommandSource::Ble, message);
  lastBleProgressUs = elapsedUs;
}

// Shared by USB streaming and BLE preview; reset after every source gap.
int32_t streamingSampleValue(const SampleRingBuffer::Record& sample) {
  if (!streamFilterEnabled) return sample.adcValue;
  if (!hasExpectedFilteredSample ||
      sample.sampleNumber != expectedFilteredSampleNumber) {
    signalFilter.reset();
  }
  const int32_t output = signalFilter.process(sample.adcValue);
  expectedFilteredSampleNumber = sample.sampleNumber + 1;
  hasExpectedFilteredSample = true;
  return output;
}

void drainStreamingSamples() {
  // Nur mit freiem USB-Sendepuffer konsumieren; Textwerte haben keine Sample-IDs.
  // Der Host kann daraus keine vollständige geräteseitige Verlustchronik ableiten.
  while (Serial.availableForWrite() >= 12) {
    SampleRingBuffer::Record sample;
    if (!adsService.popSample(sample)) {
      return;
    }

    lastSample = sample;
    hasLastSample = true;
    Serial.println(streamingSampleValue(sample));
  }
}

void restartDisplayPipeline() {
  // Nach jeder Quellenlücke wieder 500 Samples einschwingen lassen (2 s bei 250 SPS).
  signalFilter.reset();
  displayWarmupSamplesRemaining = DISPLAY_WARMUP_SAMPLES;
  displayBleService.restartSegment();
}

void drainDisplaySamples() {
  SampleRingBuffer::Record sample;
  while (adsService.popSample(sample)) {
    lastSample = sample;
    hasLastSample = true;
    if (!hasExpectedDisplaySample ||
        sample.sampleNumber != expectedDisplaySampleNumber) {
      restartDisplayPipeline();
    }

    const int32_t filtered = signalFilter.process(sample.adcValue);
    expectedDisplaySampleNumber = sample.sampleNumber + 1;
    hasExpectedDisplaySample = true;

    if (displayWarmupSamplesRemaining > 0) {
      --displayWarmupSamplesRemaining;
      if (displayWarmupSamplesRemaining == 0) {
        displayBleService.setValid(true);
      }
      continue;
    }
    displayBleService.appendSample(sample.sampleNumber, filtered);
  }
}

void sendLoggingStatus(CommandSource source) {
  const SampleRingBuffer::Snapshot snapshot = adsService.snapshot();
  char message[384];
  snprintf(message, sizeof(message),
           "state: logging\r\n"
           "logging_ms: %lu\r\n"
           "samples_received: %lu\r\n"
           "samples_processed: %lu\r\n"
           "samples_dropped: %lu\r\n"
           "sample_read_errors: %lu\r\n"
           "inferred_missed_drdy: %lu\r\n"
           "settling_samples_discarded: %lu\r\n"
           "ring_fill: %u/%u\r\n"
           "ring_max_fill: %u\r\n"
           "last_sample: %lu\r\n"
           "last_timestamp_us: %lu\r\n"
           "last_adc: %ld\r\n"
           "file: %s\r\n"
           "sd: writing\r\n",
           static_cast<unsigned long>(adsService.elapsedUs() / 1000),
           static_cast<unsigned long>(snapshot.samplesReceived),
           static_cast<unsigned long>(snapshot.samplesProcessed),
           static_cast<unsigned long>(snapshot.samplesDropped),
           static_cast<unsigned long>(snapshot.sampleReadErrors),
           static_cast<unsigned long>(snapshot.inferredMissedDrdy),
           static_cast<unsigned long>(snapshot.settlingSamplesDiscarded),
           static_cast<unsigned int>(snapshot.fill),
           static_cast<unsigned int>(SampleRingBuffer::CAPACITY - 1),
           static_cast<unsigned int>(snapshot.maxFill),
           static_cast<unsigned long>(hasLastSample ? lastSample.sampleNumber : 0),
           static_cast<unsigned long>(hasLastSample ? lastSample.timestampUs : 0),
           static_cast<long>(hasLastSample ? lastSample.adcValue : 0),
           logWriter.fileName());
  console.reply(source, message);
}

void sendLoggingSummary(const char* reason, CommandSource source,
                        bool broadcast, bool sdSuccess) {
  const SampleRingBuffer::Snapshot snapshot = adsService.snapshot();
  char message[384];
  snprintf(message, sizeof(message),
           "logging complete:\r\n"
           "reason: %s\r\n"
           "duration_ms: %lu\r\n"
           "samples_received: %lu\r\n"
           "samples_processed: %lu\r\n"
           "samples_dropped: %lu\r\n"
           "sample_read_errors: %lu\r\n"
           "inferred_missed_drdy: %lu\r\n"
           "settling_samples_discarded: %lu\r\n"
           "ring_max_fill: %u\r\n"
           "file: %s\r\n"
           "sd: %s\r\n"
           "sd_error: %s\r\n"
           "state: idle\r\n",
           reason, static_cast<unsigned long>(adsService.elapsedUs() / 1000),
           static_cast<unsigned long>(snapshot.samplesReceived),
           static_cast<unsigned long>(snapshot.samplesProcessed),
           static_cast<unsigned long>(snapshot.samplesDropped),
           static_cast<unsigned long>(snapshot.sampleReadErrors),
           static_cast<unsigned long>(snapshot.inferredMissedDrdy),
           static_cast<unsigned long>(snapshot.settlingSamplesDiscarded),
           static_cast<unsigned int>(snapshot.maxFill), logWriter.fileName(),
           sdSuccess ? "complete" : "error", logWriter.lastError());
  if (broadcast) {
    console.broadcast(message);
  } else {
    console.reply(source, message);
  }
}

void sendStreamingStatus(CommandSource source) {
  const SampleRingBuffer::Snapshot snapshot = adsService.snapshot();
  char filterBand[32];
  formatFilterBand(filterBand, sizeof(filterBand));
  char message[320];
  snprintf(message, sizeof(message),
           "state: streaming\r\n"
           "streaming_ms: %lu\r\n"
           "samples_received: %lu\r\n"
           "samples_processed: %lu\r\n"
           "samples_dropped: %lu\r\n"
           "sample_read_errors: %lu\r\n"
           "inferred_missed_drdy: %lu\r\n"
           "settling_samples_discarded: %lu\r\n"
           "ring_fill: %u/%u\r\n"
            "ring_max_fill: %u\r\n"
            "last_adc: %ld\r\n"
            "filter: %s\r\n"
           "serial: stream values\r\n",
           static_cast<unsigned long>(adsService.elapsedUs() / 1000),
           static_cast<unsigned long>(snapshot.samplesReceived),
           static_cast<unsigned long>(snapshot.samplesProcessed),
           static_cast<unsigned long>(snapshot.samplesDropped),
           static_cast<unsigned long>(snapshot.sampleReadErrors),
           static_cast<unsigned long>(snapshot.inferredMissedDrdy),
           static_cast<unsigned long>(snapshot.settlingSamplesDiscarded),
           static_cast<unsigned int>(snapshot.fill),
           static_cast<unsigned int>(SampleRingBuffer::CAPACITY - 1),
           static_cast<unsigned int>(snapshot.maxFill),
           static_cast<long>(hasLastSample ? lastSample.adcValue : 0),
            streamFilterEnabled ? filterBand : "off");
  console.reply(source, message);
}

void sendStreamingSummary(CommandSource source) {
  const SampleRingBuffer::Snapshot snapshot = adsService.snapshot();
  char message[320];
  snprintf(message, sizeof(message),
           "streaming complete:\r\n"
           "duration_ms: %lu\r\n"
           "samples_received: %lu\r\n"
           "samples_processed: %lu\r\n"
           "samples_dropped: %lu\r\n"
           "sample_read_errors: %lu\r\n"
           "inferred_missed_drdy: %lu\r\n"
           "ring_max_fill: %u\r\n"
           "state: idle\r\n",
           static_cast<unsigned long>(adsService.elapsedUs() / 1000),
           static_cast<unsigned long>(snapshot.samplesReceived),
           static_cast<unsigned long>(snapshot.samplesProcessed),
           static_cast<unsigned long>(snapshot.samplesDropped),
           static_cast<unsigned long>(snapshot.sampleReadErrors),
           static_cast<unsigned long>(snapshot.inferredMissedDrdy),
           static_cast<unsigned int>(snapshot.maxFill));
  console.reply(source, message);
}

void stopLogging(const char* reason, CommandSource source, bool broadcast) {
  // Zuerst Erzeugung stoppen, dann Restpuffer leeren und den Footer abschließen.
  adsService.stop();
  const bool drained = drainSamples();
  const SampleRingBuffer::Snapshot snapshot = adsService.snapshot();
  const bool sdSuccess =
      drained && logWriter.finish(reason, adsService.elapsedUs(), snapshot);
  if (!drained) {
    logWriter.abort();
  }
  appState = AppState::Idle;
  sendLoggingSummary(reason, source, broadcast, sdSuccess);
}

void updateLogging() {
  if (!drainSamples()) {
    stopLogging("sd_write_error", CommandSource::Serial, true);
    return;
  }

  sendLoggingProgressIfDue();
  sendBleProgressIfDue();

  const uint32_t durationUs =
      settings.current().testDurationSeconds * 1000000UL;
  if (adsService.elapsedUs() >= durationUs) {
    stopLogging("timeout", CommandSource::Serial, true);
  }
}

void stopStreaming(CommandSource source) {
  adsService.stop();
  drainStreamingSamples();
  appState = AppState::Idle;
  sendStreamingSummary(source);
  console.setSerialRepliesEnabled(true);
}

void updateStreaming() {
  drainStreamingSamples();
}

void updateDisplay() {
  drainDisplaySamples();
  displayBleService.update();
}

void sendStatus(CommandSource source) {
  if (appState == AppState::Logging) {
    sendLoggingStatus(source);
    return;
  }
  if (appState == AppState::Streaming) {
    sendStreamingStatus(source);
    return;
  }

  char message[128];
  snprintf(message, sizeof(message),
           "state: %s\r\nboot_ms: %lu\r\nunix_time: %lu\r\n",
           stateName(appState),
           static_cast<unsigned long>(timeService.bootTimeMs()),
           static_cast<unsigned long>(timeService.unixTime()));
  console.reply(source, message);
}

bool parseUnixSeconds(const char* text, uint32_t& unixSeconds) {
  if (*text == '\0') {
    return false;
  }

  uint32_t value = 0;
  for (const char* character = text; *character != '\0'; character++) {
    if (*character < '0' || *character > '9') {
      return false;
    }

    const uint8_t digit = static_cast<uint8_t>(*character - '0');
    if (value > (UINT32_MAX - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }

  unixSeconds = value;
  return true;
}

bool singleArgument(char*& savePointer, char*& argument) {
  argument = strtok_r(nullptr, " ", &savePointer);
  return argument != nullptr && strtok_r(nullptr, " ", &savePointer) == nullptr;
}

bool parseFrequencyMilliHz(const char* text, uint16_t& frequencyMilliHz) {
  // Dezimale Hz-Eingabe ohne Float-Rundung in maximal drei Nachkommastellen lesen.
  uint32_t whole = 0;
  uint32_t fraction = 0;
  uint8_t fractionDigits = 0;
  bool decimalPointSeen = false;
  bool digitSeen = false;
  for (const char* character = text; *character != '\0'; ++character) {
    if (*character == '.') {
      if (decimalPointSeen) {
        return false;
      }
      decimalPointSeen = true;
      continue;
    }
    if (*character < '0' || *character > '9') {
      return false;
    }
    digitSeen = true;
    const uint8_t digit = *character - '0';
    if (!decimalPointSeen) {
      if (whole > (UINT16_MAX - digit) / 10) {
        return false;
      }
      whole = whole * 10 + digit;
    } else {
      if (fractionDigits == 3) {
        return false;
      }
      fraction = fraction * 10 + digit;
      ++fractionDigits;
    }
  }
  if (!digitSeen) {
    return false;
  }
  while (fractionDigits < 3) {
    fraction *= 10;
    ++fractionDigits;
  }
  const uint32_t value = whole * 1000 + fraction;
  if (value > UINT16_MAX) {
    return false;
  }
  frequencyMilliHz = static_cast<uint16_t>(value);
  return true;
}

void formatFilterBand(char* destination, size_t destinationSize) {
  const uint16_t low = settings.filterLowCutoffMilliHz();
  const uint16_t high = settings.filterHighCutoffMilliHz();
  snprintf(destination, destinationSize, "%u.%03u-%u.%03u Hz",
           low / 1000, low % 1000, high / 1000, high % 1000);
}

bool settingsAllowed(CommandSource source) {
  if (appState == AppState::Idle) {
    return true;
  }

  console.reply(source, "error: settings are available only in idle\r\n");
  return false;
}

void sendConfig(CommandSource source) {
  // Hostparser erwarten config: bis apply:; Feldnamen nicht beiläufig umbenennen.
  const MeasurementSettings& current = settings.current();
  char filterBand[32];
  formatFilterBand(filterBand, sizeof(filterBand));
  char message[320];
  snprintf(message, sizeof(message),
           "config:\r\n"
           "  firmware: %s\r\n"
           "  rate: %u SPS\r\n"
           "  gain: x%u\r\n"
           "  input: %s\r\n"
            "  rld: %s\r\n"
            "  test_duration: %lu seconds\r\n"
            "  filter_band: %s\r\n"
            "  stream_filter: %s\r\n"
           "  preview_max_rate: 500 SPS\r\n"
           "  apply: at next start\r\n",
           FIRMWARE_VERSION,
           static_cast<unsigned int>(settings.sampleRateSps()),
           static_cast<unsigned int>(settings.gainMultiplier()),
            settings.inputName(),
            current.rldEnabled ? "on (bench test only)" : "off",
            static_cast<unsigned long>(current.testDurationSeconds),
            filterBand, streamFilterEnabled ? filterBand : "off");
  console.reply(source, message);
}

void handleCommand(ConsoleCommand& command) {
  // Zustandsprüfung ist auch im Gerät nötig, unabhängig von gesperrten UI-Buttons.
  // Displaymodus ist absichtlich dauerhaft bis zum Neustart, keine NUS-Konsole.
  if (appState == AppState::Display) {
    return;
  }
  if (strcmp(command.text, "!line-too-long") == 0) {
    console.reply(command.source, "error: command too long\r\n");
    return;
  }

  char* savePointer = nullptr;
  const char* name = strtok_r(command.text, " ", &savePointer);
  if (name == nullptr) {
    return;
  }

  if (strcmp(name, "help") == 0) {
    sendHelp(command.source);
    return;
  }

  if (strcmp(name, "state") == 0) {
    char message[32];
    snprintf(message, sizeof(message), "state: %s\r\n",
             stateName(appState));
    console.reply(command.source, message);
    return;
  }

  if (strcmp(name, "status") == 0) {
    sendStatus(command.source);
    return;
  }

  if (strcmp(name, "time") == 0) {
    char* argument = strtok_r(nullptr, " ", &savePointer);
    char* extraArgument = strtok_r(nullptr, " ", &savePointer);
    if (argument == nullptr || extraArgument != nullptr) {
      console.reply(command.source, "usage: time <unix_seconds>\r\n");
      return;
    }

    uint32_t unixSeconds = 0;
    if (!parseUnixSeconds(argument, unixSeconds)) {
      console.reply(command.source, "error: invalid unix time\r\n");
      return;
    }

    timeService.setUnixTime(unixSeconds);
    console.reply(command.source, "time: set\r\n");
    return;
  }

  if (strcmp(name, "config") == 0) {
    if (!settingsAllowed(command.source)) {
      return;
    }
    char* action = strtok_r(nullptr, " ", &savePointer);
    if (action == nullptr) {
      sendConfig(command.source);
      return;
    }
    if (strcmp(action, "reset") != 0 ||
        strtok_r(nullptr, " ", &savePointer) != nullptr) {
      console.reply(command.source, "usage: config [reset]\r\n");
      return;
    }
    if (!settings.reset()) {
      console.reply(command.source, "error: config reset was not saved\r\n");
      return;
    }
    streamFilterEnabled = false;
    console.reply(command.source, "config: reset to defaults\r\n");
    return;
  }

  if (strcmp(name, "gain") == 0 || strcmp(name, "rate") == 0 ||
      strcmp(name, "test_duration") == 0) {
    if (!settingsAllowed(command.source)) {
      return;
    }

    char* argument = nullptr;
    uint32_t value = 0;
    if (!singleArgument(savePointer, argument) ||
        !parseUnixSeconds(argument, value)) {
      console.reply(command.source, "error: expected one positive number\r\n");
      return;
    }

    const bool configured =
        strcmp(name, "gain") == 0 ? settings.setGain(value)
        : strcmp(name, "rate") == 0 ? settings.setSampleRate(value)
                                    : settings.setTestDurationSeconds(value);
    if (!configured) {
      console.reply(command.source, "error: unsupported value\r\n");
      return;
    }
    if (!settings.save()) {
      console.reply(command.source, "error: setting was not saved\r\n");
      return;
    }

    console.reply(command.source, "setting: updated and saved\r\n");
    return;
  }

  if (strcmp(name, "input") == 0) {
    if (!settingsAllowed(command.source)) {
      return;
    }

    char* argument = nullptr;
    if (!singleArgument(savePointer, argument) || !settings.setInput(argument)) {
      console.reply(command.source,
                    "usage: input <normal|short|test>\r\n");
      return;
    }
    if (!settings.save()) {
      console.reply(command.source, "error: setting was not saved\r\n");
      return;
    }

    console.reply(command.source, "setting: updated and saved\r\n");
    return;
  }

  if (strcmp(name, "rld") == 0) {
    if (!settingsAllowed(command.source)) {
      return;
    }

    char* argument = nullptr;
    if (!singleArgument(savePointer, argument) ||
        (strcmp(argument, "on") != 0 && strcmp(argument, "off") != 0)) {
      console.reply(command.source, "usage: rld <on|off>\r\n");
      return;
    }

    const bool rldEnabled = strcmp(argument, "on") == 0;
    const bool previousRldEnabled = settings.current().rldEnabled;
    settings.setRldEnabled(rldEnabled);
    if (!settings.save()) {
      settings.setRldEnabled(previousRldEnabled);
      console.reply(command.source, "error: setting was not saved\r\n");
      return;
    }
    console.reply(command.source,
                  rldEnabled ? "setting: updated and saved (applied at next start; "
                                "bench test only)\r\n"
                              : "setting: updated and saved (applied at next start)\r\n");
    return;
  }

  if (strcmp(name, "filter") == 0) {
    if (!settingsAllowed(command.source)) {
      return;
    }

    char* action = strtok_r(nullptr, " ", &savePointer);
    if (action == nullptr) {
      console.reply(command.source,
                    "usage: filter <on|off>|band <low_hz> <high_hz>\r\n");
      return;
    }

    if (strcmp(action, "band") == 0) {
      char* lowArgument = strtok_r(nullptr, " ", &savePointer);
      char* highArgument = strtok_r(nullptr, " ", &savePointer);
      if (lowArgument == nullptr || highArgument == nullptr ||
          strtok_r(nullptr, " ", &savePointer) != nullptr) {
        console.reply(command.source, "usage: filter band <low_hz> <high_hz>\r\n");
        return;
      }
      uint16_t lowCutoffMilliHz = 0;
      uint16_t highCutoffMilliHz = 0;
      if (!parseFrequencyMilliHz(lowArgument, lowCutoffMilliHz) ||
          !parseFrequencyMilliHz(highArgument, highCutoffMilliHz) ||
          !settings.setFilterBandMilliHz(lowCutoffMilliHz, highCutoffMilliHz)) {
        console.reply(command.source, "error: unsupported filter band\r\n");
        return;
      }
      if (!settings.save()) {
        console.reply(command.source, "error: setting was not saved\r\n");
        return;
      }
      console.reply(command.source,
                    "setting: filter band updated and saved (applied at next start)\r\n");
      return;
    }

    if (strtok_r(nullptr, " ", &savePointer) != nullptr ||
        (strcmp(action, "on") != 0 && strcmp(action, "off") != 0)) {
      console.reply(command.source,
                    "usage: filter <on|off>|band <low_hz> <high_hz>\r\n");
      return;
    }

    streamFilterEnabled = strcmp(action, "on") == 0;
    console.reply(command.source,
                  "setting: updated (applied at next stream or preview start)\r\n");
    return;
  }

  // Internal web UI controls: intentionally omitted from the public help list.
  if (strcmp(name, "preview") == 0) {
    char* action = nullptr;
    if (!singleArgument(savePointer, action) ||
        (strcmp(action, "start") != 0 && strcmp(action, "stop") != 0)) {
      console.reply(command.source, "error: invalid preview command\r\n");
      return;
    }
    if (strcmp(action, "stop") == 0) {
      if (appState != AppState::Preview) {
        console.reply(command.source, "error: preview is not running\r\n");
        return;
      }
      stopPreview(command.source, true);
      return;
    }
    if (appState != AppState::Idle || !previewBleService.subscribed()) {
      console.reply(command.source, "error: preview requires idle and a BLE subscriber\r\n");
      return;
    }
    hasExpectedFilteredSample = false;
    if (streamFilterEnabled &&
        !signalFilter.configure(settings.sampleRateSps(),
                                settings.filterLowCutoffMilliHz(),
                                settings.filterHighCutoffMilliHz())) {
      console.reply(command.source, "error: unsupported filter sample rate\r\n");
      return;
    }
    if (!adsService.start(settings.current())) {
      console.reply(command.source, "error: preview ADS start failed\r\n");
      return;
    }
    hasLastSample = false;
    previewDisconnectPending = false;
    previewBleService.start(settings.sampleRateSps(), streamFilterEnabled);
    appState = AppState::Preview;
    console.reply(command.source, "preview: started\r\n");
    return;
  }

  if (strcmp(name, "start") == 0 || strcmp(name, "record") == 0) {
    RecordingMetadata metadata;
    const bool webRecording = strcmp(name, "record") == 0;
    if (webRecording) {
      char* argument = nullptr;
      uint32_t token = 0;
      if (command.source != CommandSource::Ble || appState != AppState::Files ||
          !singleArgument(savePointer, argument) || !parseUnixSeconds(argument, token) ||
          !sdFileBleService.consumeMetadata(token, metadata)) {
        console.reply(command.source, "error: recording metadata not confirmed or expired\r\n");
        return;
      }
      appState = AppState::Idle;
    } else if (strtok_r(nullptr, " ", &savePointer) != nullptr) {
      console.reply(command.source, "usage: start\r\n");
      return;
    }
    if (appState == AppState::Preview) stopPreview(command.source, true);
    if (appState != AppState::Idle) {
      console.reply(command.source, "error: start is available only in idle\r\n");
      return;
    }
    // SD-Datei samt Header vorbereiten, BEVOR der ADC neue Samples produziert.
    if (!logWriter.begin(settings.current(), settings.sampleRateSps(),
                         settings.gainMultiplier(), timeService,
                          FIRMWARE_VERSION, webRecording ? &metadata : nullptr)) {
      char message[128];
      snprintf(message, sizeof(message), "error: %s\r\n",
               logWriter.lastError());
      console.reply(command.source, message);
      return;
    }

    hasLastSample = false;
    if (!adsService.start(settings.current())) {
      logWriter.abort();
      char message[128];
      snprintf(message, sizeof(message), "error: %s\r\n",
               adsService.lastError());
      console.reply(command.source, message);
      return;
    }

    appState = AppState::Logging;
    lastLoggingProgressUs = 0;
    lastBleProgressUs = 0;
    char message[64];
    snprintf(message, sizeof(message), "logging: started\r\nfile: %s\r\n",
             logWriter.fileName());
    console.reply(command.source, message);
    return;
  }

  if (strcmp(name, "stream") == 0) {
    char* action = nullptr;
    if (!singleArgument(savePointer, action) ||
        (strcmp(action, "start") != 0 && strcmp(action, "stop") != 0)) {
      console.reply(command.source, "usage: stream <start|stop>\r\n");
      return;
    }

    if (strcmp(action, "start") == 0) {
      if (appState != AppState::Idle) {
        console.reply(command.source,
                      "error: stream start is available only in idle\r\n");
        return;
      }

      hasLastSample = false;
      hasExpectedFilteredSample = false;
      if (streamFilterEnabled &&
          !signalFilter.configure(settings.sampleRateSps(),
                                  settings.filterLowCutoffMilliHz(),
                                  settings.filterHighCutoffMilliHz())) {
        console.reply(command.source, "error: unsupported filter sample rate\r\n");
        return;
      }
      if (!adsService.start(settings.current())) {
        char message[128];
        snprintf(message, sizeof(message), "error: %s\r\n",
                 adsService.lastError());
        console.reply(command.source, message);
        return;
      }

      appState = AppState::Streaming;
      console.setSerialRepliesEnabled(false);
      console.reply(command.source, "streaming: started\r\n");
      return;
    }

    if (appState != AppState::Streaming) {
      console.reply(command.source,
                    "error: stream stop is available only while streaming\r\n");
      return;
    }

    stopStreaming(command.source);
    return;
  }

  if (strcmp(name, "display") == 0) {
    char* action = nullptr;
    if (!singleArgument(savePointer, action) || strcmp(action, "start") != 0) {
      console.reply(command.source, "usage: display start\r\n");
      return;
    }
    if (appState != AppState::Idle) {
      console.reply(command.source,
                    "error: display start is available only in idle\r\n");
      return;
    }
    if (settings.sampleRateSps() != DISPLAY_SAMPLE_RATE_SPS) {
      console.reply(command.source, "error: display requires rate 250\r\n");
      return;
    }
    if (!signalFilter.configure(DISPLAY_SAMPLE_RATE_SPS,
                                settings.filterLowCutoffMilliHz(),
                                settings.filterHighCutoffMilliHz())) {
      console.reply(command.source, "error: display filter setup failed\r\n");
      return;
    }

    hasLastSample = false;
    hasExpectedDisplaySample = false;
    displayWarmupSamplesRemaining = DISPLAY_WARMUP_SAMPLES;
    displayBleService.start(DISPLAY_SAMPLE_RATE_SPS,
                            settings.filterLowCutoffMilliHz(),
                            settings.filterHighCutoffMilliHz());
    if (!adsService.start(settings.current())) {
      char message[128];
      snprintf(message, sizeof(message), "error: %s\r\n",
               adsService.lastError());
      console.reply(command.source, message);
      return;
    }

    appState = AppState::Display;
    if (!configureAdvertising(displayBleService.service(), "Bike_EKG_Display",
                              true)) {
      adsService.stop();
      appState = AppState::Idle;
      console.reply(command.source, "error: display BLE setup failed\r\n");
      return;
    }
    console.reply(command.source,
                  "display: started; NUS will disconnect\r\n");
    return;
  }

  if (strcmp(name, "stop") == 0) {
    if (appState != AppState::Logging) {
      console.reply(command.source, "error: stop is available only while logging\r\n");
      return;
    }
    if (strtok_r(nullptr, " ", &savePointer) != nullptr) {
      console.reply(command.source, "usage: stop\r\n");
      return;
    }

    stopLogging("user_stop", command.source, false);
    return;
  }

  console.reply(command.source, "error: unknown command; use help\r\n");
}

void updatePreviewStatistics() {
  // Exklusives Ende des Quellnummernraums, nicht nur Anzahl gesendeter Samples.
  // Der Browser erkennt damit auch einen fehlenden Rest nach dem letzten Paket.
  const SampleRingBuffer::Snapshot snapshot = adsService.snapshot();
  const uint32_t sourceEnd = snapshot.samplesReceived - snapshot.settlingSamplesDiscarded +
                            snapshot.sampleReadErrors + snapshot.inferredMissedDrdy;
  previewBleService.setStatistics(sourceEnd, snapshot.samplesDropped);
}

void stopPreview(CommandSource source, bool reply) {
  adsService.stop();
  SampleRingBuffer::Record sample;
  while (adsService.popSample(sample)) {
    previewBleService.append(sample.sampleNumber, streamingSampleValue(sample));
  }
  updatePreviewStatistics();
  previewBleService.stop();
  appState = AppState::Idle;
  if (reply) console.reply(source, "preview: stopped\r\nstate: idle\r\n");
}

void updatePreview() {
  if (previewDisconnectPending || !previewBleService.subscribed()) {
    previewDisconnectPending = false;
    stopPreview(CommandSource::Ble, false);
    return;
  }
  SampleRingBuffer::Record sample;
  // Bound main-loop work so commands, disconnects and BLE TX remain responsive.
  for (uint16_t count = 0; count < 128 && adsService.popSample(sample); ++count) {
    lastSample = sample;
    hasLastSample = true;
    previewBleService.append(sample.sampleNumber, streamingSampleValue(sample));
  }
  updatePreviewStatistics();
  previewBleService.update();
}

void setup() {
  Serial.begin(115200);
  beginBle();
  if (!settings.begin()) {
    Serial.println("warning: persistent settings unavailable; using defaults");
  }
  if (!timeService.begin()) {
    Serial.println("error: timestamp timer setup failed");
  }
  Serial.print("Bike EKG firmware ");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("state: init");

  appState = AppState::Idle;
  Serial.println("state: idle");
}

void updateSdFiles() {
  sdFileBleService.update(appState == AppState::Idle || appState == AppState::Files);
  if (sdFileBleService.active() && appState == AppState::Idle) appState = AppState::Files;
  else if (!sdFileBleService.active() && appState == AppState::Files) appState = AppState::Idle;
}

void loop() {
  // Messpuffer vor und nach der Konsolenarbeit bedienen, um Rückstau zu begrenzen.
  updateSdFiles();
  updateAdvertisingRestart();
  if (appState == AppState::Logging) {
    updateLogging();
  } else if (appState == AppState::Streaming) {
    updateStreaming();
  } else if (appState == AppState::Display) {
    updateDisplay();
  } else if (appState == AppState::Preview) {
    updatePreview();
  }

  ConsoleCommand command;
  if (console.poll(command)) {
    handleCommand(command);
  }
  updateSdFiles();

  if (appState == AppState::Logging) {
    updateLogging();
  } else if (appState == AppState::Streaming) {
    updateStreaming();
  } else if (appState == AppState::Display) {
    updateDisplay();
  } else if (appState == AppState::Preview) {
    updatePreview();
  }
}
