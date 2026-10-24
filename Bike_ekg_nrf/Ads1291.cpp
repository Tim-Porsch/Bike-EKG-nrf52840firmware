#include "Ads1291.h"

// Bewusst softwaregetaktetes SPI über GPIO; Pinmasken gelten nur für diese PCB.
// Die Wartezeiten sind Teil des Geräteprotokolls, keine UI-Verzögerungen.

namespace {
constexpr uint32_t SCLK_MASK = 1UL << 6;   // P0.06
constexpr uint32_t DIN_MASK = 1UL << 8;    // P0.08
constexpr uint8_t DOUT_PIN = 11;           // P0.11
constexpr uint32_t DOUT_MASK = 1UL << DOUT_PIN;
constexpr uint32_t CS_MASK = 1UL << 6;     // P1.06
constexpr uint32_t RESET_MASK = 1UL << 13; // P1.13, PWDN/RESET
constexpr uint32_t START_MASK = 1UL << 15; // P1.15
constexpr uint8_t DRDY_PIN = 7;            // P1.07
constexpr uint32_t DRDY_MASK = 1UL << DRDY_PIN;

constexpr uint8_t COMMAND_SDATAC = 0x11;
constexpr uint8_t COMMAND_RDATA = 0x12;
constexpr uint8_t COMMAND_OFFSETCAL = 0x1A;
constexpr uint8_t COMMAND_RREG = 0x20;
constexpr uint8_t COMMAND_WREG = 0x40;
constexpr uint8_t REGISTER_ID = 0x00;
constexpr uint8_t REGISTER_CONFIG1 = 0x01;
constexpr uint8_t REGISTER_CONFIG2 = 0x02;
constexpr uint8_t REGISTER_LOFF = 0x03;
constexpr uint8_t REGISTER_CH1SET = 0x04;
constexpr uint8_t REGISTER_CH2SET = 0x05;
constexpr uint8_t REGISTER_RLD_SENS = 0x06;
constexpr uint8_t REGISTER_LOFF_SENS = 0x07;
constexpr uint8_t REGISTER_LOFF_STAT = 0x08;
constexpr uint8_t REGISTER_RESP1 = 0x09;
constexpr uint8_t REGISTER_RESP2 = 0x0A;
constexpr uint8_t EXPECTED_ID = 0x52;

constexpr uint8_t CONFIG1_SAMPLE_RATE_MASK = 0x07;
constexpr uint8_t CONFIG2_LOFF_COMPARATOR_ENABLE = 0x40;
constexpr uint8_t CONFIG2_REFERENCE_MASK = 0x30;
constexpr uint8_t CONFIG2_INTERNAL_REFERENCE_2V42 = 0x20;
constexpr uint8_t CONFIG2_TEST_SIGNAL_MASK = 0x03;
constexpr uint8_t CONFIG2_TEST_SIGNAL_ENABLE = 0x02;
constexpr uint8_t RESP1_ADS1291_REQUIRED = 0x02;
constexpr uint8_t RESP2_CALIBRATION_ENABLE = 0x80;
constexpr uint8_t RESP2_ADS1291_REQUIRED = 0x07;
constexpr uint8_t CH1SET_GAIN_MASK = 0x70;
constexpr uint8_t CH1SET_MUX_MASK = 0x0F;
constexpr uint8_t UNUSED_CHANNEL_INPUT_SHORTED = 0x01;
constexpr uint8_t RLD_SENS_BUFFER_ENABLE = 0x20;
constexpr uint8_t RLD_SENS_CH1_NEGATIVE = 0x02;
constexpr uint8_t RLD_SENS_CH1_POSITIVE = 0x01;
constexpr uint8_t LOFF_RESERVED_BITS = 0x10;
constexpr uint8_t LOFF_SENS_FLIP1 = 0x10;
constexpr uint8_t LOFF_SENS_CH1_NEGATIVE = 0x02;
constexpr uint8_t LOFF_SENS_CH1_POSITIVE = 0x01;
constexpr uint8_t LOFF_STAT_CH1_NEGATIVE = 0x02;
constexpr uint8_t LOFF_STAT_CH1_POSITIVE = 0x01;
constexpr uint32_t STATUS_HEADER_MASK = 0xF00000;
constexpr uint32_t STATUS_HEADER_VALUE = 0xC00000;
} // namespace

bool Ads1291::begin() {
  NRF_P0->OUTCLR = SCLK_MASK | DIN_MASK;
  NRF_P0->DIRSET = SCLK_MASK | DIN_MASK;
  // DIR=input, INPUT=connected, PULL=disabled, DRIVE=S0S1, SENSE=disabled.
  NRF_P0->PIN_CNF[DOUT_PIN] = 0;

  NRF_P1->OUTSET = CS_MASK | RESET_MASK;
  NRF_P1->OUTCLR = START_MASK;
  NRF_P1->DIRSET = CS_MASK | RESET_MASK | START_MASK;
  // DIR=input, INPUT=connected, PULL=disabled, DRIVE=S0S1, SENSE=disabled.
  NRF_P1->PIN_CNF[DRDY_PIN] = 0;

  delay(1000);

  NRF_P1->OUTCLR = RESET_MASK;
  delay(2);
  NRF_P1->OUTSET = RESET_MASK;
  delay(10);

  sendCommand(COMMAND_SDATAC);
  delay(10);

  const bool referenceConfigured =
      updateRegister(REGISTER_CONFIG2, CONFIG2_REFERENCE_MASK,
                     CONFIG2_INTERNAL_REFERENCE_2V42);
  delay(100);

  // TI requires the unused channel 2 MUX to be shorted on the ADS1291.
  const bool unusedChannelConfigured =
      writeRegister(REGISTER_CH2SET, UNUSED_CHANNEL_INPUT_SHORTED);
  const bool rldDisabled = writeRegister(REGISTER_RLD_SENS, 0x00);
  const bool respirationRegistersConfigured =
      writeRegister(REGISTER_RESP1, RESP1_ADS1291_REQUIRED) &&
      writeRegister(REGISTER_RESP2, RESP2_ADS1291_REQUIRED);

  return referenceConfigured && unusedChannelConfigured && rldDisabled &&
         respirationRegistersConfigured;
}

bool Ads1291::setSampleRate(SampleRate sampleRate) {
  return updateRegister(REGISTER_CONFIG1, CONFIG1_SAMPLE_RATE_MASK,
                        static_cast<uint8_t>(sampleRate));
}

bool Ads1291::setGain(Gain gain) {
  const uint8_t channelConfig = readRegister(REGISTER_CH1SET);
  if ((channelConfig & CH1SET_MUX_MASK) ==
          static_cast<uint8_t>(InputMux::SupplyMeasurement) &&
      gain != Gain::X1) {
    return false;
  }

  const uint8_t updatedConfig =
      (channelConfig & static_cast<uint8_t>(~CH1SET_GAIN_MASK)) |
      static_cast<uint8_t>(gain);
  return writeRegister(REGISTER_CH1SET, updatedConfig);
}

bool Ads1291::setInputMux(InputMux inputMux) {
  if (inputMux == InputMux::SupplyMeasurement &&
      (readRegister(REGISTER_CH1SET) & CH1SET_GAIN_MASK) !=
          static_cast<uint8_t>(Gain::X1)) {
    return false;
  }

  return updateRegister(REGISTER_CH1SET, CH1SET_MUX_MASK,
                        static_cast<uint8_t>(inputMux));
}

bool Ads1291::configureChannel(Gain gain, InputMux inputMux) {
  if (inputMux == InputMux::SupplyMeasurement && gain != Gain::X1) {
    return false;
  }

  const uint8_t channelConfig =
      static_cast<uint8_t>(gain) | static_cast<uint8_t>(inputMux);
  return writeRegister(REGISTER_CH1SET, channelConfig);
}

bool Ads1291::configureTestSignal(bool enabled,
                                   TestSignalFrequency frequency) {
  const uint8_t value =
      enabled ? CONFIG2_TEST_SIGNAL_ENABLE | static_cast<uint8_t>(frequency)
              : 0x00;
  return updateRegister(REGISTER_CONFIG2, CONFIG2_TEST_SIGNAL_MASK, value);
}

bool Ads1291::configureRld(bool enabled) {
  const uint8_t rldSense =
      enabled ? RLD_SENS_BUFFER_ENABLE | RLD_SENS_CH1_NEGATIVE |
                    RLD_SENS_CH1_POSITIVE
              : 0x00;
  return writeRegister(REGISTER_RLD_SENS, rldSense);
}

bool Ads1291::startConversions() {
  if (readId() != EXPECTED_ID) {
    return false;
  }

  NRF_P1->OUTSET = START_MASK;
  return true;
}

void Ads1291::stopConversions() {
  NRF_P1->OUTCLR = START_MASK;
}

bool Ads1291::dataReady() const {
  return (NRF_P1->IN & DRDY_MASK) == 0;
}

bool Ads1291::readSample(Sample& sample) {
  if (!dataReady()) {
    return false;
  }

  NRF_P1->OUTCLR = CS_MASK;
  delayMicroseconds(2);
  transfer(COMMAND_RDATA);

  uint32_t status = 0;
  for (uint8_t index = 0; index < 3; index++) {
    status = (status << 8) | transfer(0x00);
  }

  uint32_t channel = 0;
  for (uint8_t index = 0; index < 3; index++) {
    channel = (channel << 8) | transfer(0x00);
  }

  delayMicroseconds(10);
  NRF_P1->OUTSET = CS_MASK;
  delayMicroseconds(10);

  if ((status & STATUS_HEADER_MASK) != STATUS_HEADER_VALUE) {
    return false;
  }

  // Das 24-Bit-Zweierkomplement muss vor der int32-Ausgabe vorzeichenerweitert werden.
  if (channel & 0x800000) {
    channel |= 0xFF000000;
  }

  sample.status = status;
  sample.channel1 = static_cast<int32_t>(channel);
  return true;
}

bool Ads1291::calibrateOffset() {
  const bool calibrationEnabled =
      updateRegister(REGISTER_RESP2, RESP2_CALIBRATION_ENABLE,
                     RESP2_CALIBRATION_ENABLE);
  if (!calibrationEnabled) {
    return false;
  }

  sendCommand(COMMAND_OFFSETCAL);
  delay(10);

  return updateRegister(REGISTER_RESP2, RESP2_CALIBRATION_ENABLE, 0x00);
}

bool Ads1291::configureLeadOff(const LeadOffConfig& config) {
  const uint8_t leadOffConfig =
      static_cast<uint8_t>(config.threshold) | LOFF_RESERVED_BITS |
      static_cast<uint8_t>(config.current) |
      static_cast<uint8_t>(config.mode);

  uint8_t leadOffSense = config.reverseCurrent ? LOFF_SENS_FLIP1 : 0;
  if (config.detectPositive) {
    leadOffSense |= LOFF_SENS_CH1_POSITIVE;
  }
  if (config.detectNegative) {
    leadOffSense |= LOFF_SENS_CH1_NEGATIVE;
  }

  const bool isDc = config.mode == LeadOffMode::Dc;
  const bool controlConfigured = writeRegister(REGISTER_LOFF, leadOffConfig);
  const bool comparatorConfigured =
      updateRegister(REGISTER_CONFIG2, CONFIG2_LOFF_COMPARATOR_ENABLE,
                     isDc ? CONFIG2_LOFF_COMPARATOR_ENABLE : 0);
  const bool senseConfigured =
      writeRegister(REGISTER_LOFF_SENS, leadOffSense);
  const bool configured =
      controlConfigured && comparatorConfigured && senseConfigured;

  return configured;
}

bool Ads1291::disableLeadOff() {
  const bool senseDisabled = writeRegister(REGISTER_LOFF_SENS, 0x00);
  const bool comparatorDisabled =
      updateRegister(REGISTER_CONFIG2, CONFIG2_LOFF_COMPARATOR_ENABLE, 0x00);
  const bool disabled = senseDisabled && comparatorDisabled;

  return disabled;
}

Ads1291::LeadOffStatus Ads1291::readLeadOffStatus() {
  LeadOffStatus status;
  uint8_t leadOffControl = 0;
  uint8_t config2 = 0;
  uint8_t leadOffSense = 0;
  uint8_t rawStatus = 0;

  const bool controlValid =
      readRegisterStable(REGISTER_LOFF, leadOffControl);
  const bool configValid = readRegisterStable(REGISTER_CONFIG2, config2);
  const bool senseValid =
      readRegisterStable(REGISTER_LOFF_SENS, leadOffSense);
  const bool statusValid =
      readRegisterStable(REGISTER_LOFF_STAT, rawStatus);

  const bool positiveEnabled = leadOffSense & LOFF_SENS_CH1_POSITIVE;
  const bool negativeEnabled = leadOffSense & LOFF_SENS_CH1_NEGATIVE;
  const bool dcMode = (leadOffControl & 0x01) == 0;
  const bool comparatorEnabled =
      config2 & CONFIG2_LOFF_COMPARATOR_ENABLE;

  status.valid = controlValid && configValid && senseValid && statusValid &&
                 readId() == EXPECTED_ID && dcMode && comparatorEnabled &&
                 (positiveEnabled || negativeEnabled);

  if (!status.valid) {
    return status;
  }

  status.positiveDisconnected =
      positiveEnabled && (rawStatus & LOFF_STAT_CH1_POSITIVE);
  status.negativeDisconnected =
      negativeEnabled && (rawStatus & LOFF_STAT_CH1_NEGATIVE);

  return status;
}

uint8_t Ads1291::readId() {
  return readRegister(REGISTER_ID);
}

bool Ads1291::checkCommunication(uint8_t& id) {
  id = readId();
  const uint8_t secondId = readId();
  const uint8_t thirdId = readId();

  return id == EXPECTED_ID && secondId == EXPECTED_ID &&
         thirdId == EXPECTED_ID;
}

uint8_t Ads1291::transfer(uint8_t output) {
  uint8_t input = 0;

  for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
    // SPI mode 1: data changes on the rising and is sampled on the falling edge.
    NRF_P0->OUTSET = SCLK_MASK;
    if (output & mask) {
      NRF_P0->OUTSET = DIN_MASK;
    } else {
      NRF_P0->OUTCLR = DIN_MASK;
    }

    delayMicroseconds(2);
    NRF_P0->OUTCLR = SCLK_MASK;
    delayMicroseconds(2);

    input <<= 1;
    if (NRF_P0->IN & DOUT_MASK) {
      input |= 1;
    }
    delayMicroseconds(2);
  }

  return input;
}

void Ads1291::sendCommand(uint8_t command) {
  NRF_P1->OUTCLR = CS_MASK;
  delayMicroseconds(2);
  transfer(command);
  delayMicroseconds(10);
  NRF_P1->OUTSET = CS_MASK;
  delayMicroseconds(10);
}

uint8_t Ads1291::readRegister(uint8_t address) {
  NRF_P1->OUTCLR = CS_MASK;
  delayMicroseconds(2);
  transfer(COMMAND_RREG | (address & 0x1F));
  delayMicroseconds(10);
  transfer(0x00); // Number of registers minus one.
  delayMicroseconds(10);
  const uint8_t value = transfer(0x00);
  delayMicroseconds(10);
  NRF_P1->OUTSET = CS_MASK;
  delayMicroseconds(10);

  return value;
}

bool Ads1291::readRegisterStable(uint8_t address, uint8_t& value) {
  const uint8_t firstValue = readRegister(address);
  const uint8_t secondValue = readRegister(address);
  const uint8_t thirdValue = readRegister(address);
  value = thirdValue;

  return firstValue == secondValue && secondValue == thirdValue;
}

bool Ads1291::writeRegister(uint8_t address, uint8_t value) {
  // Rücklesen und Chip-ID prüfen: eine gesendete Bitfolge allein bestätigt nichts.
  NRF_P1->OUTCLR = CS_MASK;
  delayMicroseconds(2);
  transfer(COMMAND_WREG | (address & 0x1F));
  delayMicroseconds(10);
  transfer(0x00); // Number of registers minus one.
  delayMicroseconds(10);
  transfer(value);
  delayMicroseconds(10);
  NRF_P1->OUTSET = CS_MASK;
  delayMicroseconds(10);

  return readRegister(address) == value && readId() == EXPECTED_ID;
}

bool Ads1291::updateRegister(uint8_t address, uint8_t mask, uint8_t value) {
  // Nur die adressierten Bits ändern; andere Funktionen teilen dasselbe Register.
  const uint8_t currentValue = readRegister(address);
  const uint8_t updatedValue =
      (currentValue & static_cast<uint8_t>(~mask)) | (value & mask);
  return writeRegister(address, updatedValue);
}
