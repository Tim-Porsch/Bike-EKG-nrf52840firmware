#pragma once

// Hardwaretreiber für den ADS1291 auf der projektspezifischen nRF-Platine.
// Enum-Werte entsprechen Registerbits, nicht direkt SPS oder Gain-Faktoren.
// Der Aufrufer koordiniert Konfiguration und laufende Konversionen.

#include <Arduino.h>

// Basic use:
//   ads.begin();
//   ads.setSampleRate(Ads1291::SampleRate::Sps500);
//   ads.configureChannel(Ads1291::Gain::X6,
//                        Ads1291::InputMux::NormalElectrode);
// Lead-off:
//   Ads1291::LeadOffConfig leadOff;
//   ads.configureLeadOff(leadOff);
//   Ads1291::LeadOffStatus status = ads.readLeadOffStatus();
class Ads1291 {
public:
  enum class Gain : uint8_t {
    X1 = 0x10,
    X2 = 0x20,
    X3 = 0x30,
    X4 = 0x40,
    X6 = 0x00,
    X8 = 0x50,
    X12 = 0x60,
  };

  enum class SampleRate : uint8_t {
    Sps125 = 0x00,
    Sps250 = 0x01,
    Sps500 = 0x02,
    Sps1000 = 0x03,
    Sps2000 = 0x04,
    Sps4000 = 0x05,
    Sps8000 = 0x06,
  };

  enum class InputMux : uint8_t {
    NormalElectrode = 0x00,
    InputShorted = 0x01,
    RldMeasure = 0x02,
    SupplyMeasurement = 0x03,
    TemperatureSensor = 0x04,
    TestSignal = 0x05,
    RldPositive = 0x06,
    RldNegative = 0x07,
    RldBoth = 0x08,
    Inputs3 = 0x09,
  };

  enum class TestSignalFrequency : uint8_t {
    Dc = 0x00,
    Hz1 = 0x01,
  };

  enum class LeadOffCurrent : uint8_t {
    Nanoamps6 = 0x00,
    Nanoamps22 = 0x04,
    Microamps6 = 0x08,
    Microamps22 = 0x0C,
  };

  enum class LeadOffThreshold : uint8_t {
    Percent95And5 = 0x00,
    Percent92_5And7_5 = 0x20,
    Percent90And10 = 0x40,
    Percent87_5And12_5 = 0x60,
    Percent85And15 = 0x80,
    Percent80And20 = 0xA0,
    Percent75And25 = 0xC0,
    Percent70And30 = 0xE0,
  };

  enum class LeadOffMode : uint8_t {
    Dc = 0x00,
    AcQuarterSampleRate = 0x01,
  };

  struct LeadOffConfig {
    LeadOffCurrent current = LeadOffCurrent::Nanoamps6;
    LeadOffThreshold threshold = LeadOffThreshold::Percent95And5;
    LeadOffMode mode = LeadOffMode::Dc;
    bool detectPositive = true;
    bool detectNegative = true;
    bool reverseCurrent = false;
  };

  struct LeadOffStatus {
    // Comparator status is available only for enabled DC lead-off detection.
    bool valid = false;
    bool positiveDisconnected = false;
    bool negativeDisconnected = false;
  };

  struct Sample {
    uint32_t status = 0;
    int32_t channel1 = 0;
  };

  // Call once before any other command. Enables the internal 2.42 V reference
  // and leaves the ADS1291 in SDATAC mode. Returns false on SPI/config failure.
  bool begin();

  // All setters preserve unrelated register bits and verify the written value.
  bool setSampleRate(SampleRate sampleRate);
  bool setGain(Gain gain);
  bool setInputMux(InputMux inputMux);
  bool configureChannel(Gain gain, InputMux inputMux);
  bool configureTestSignal(
      bool enabled, TestSignalFrequency frequency = TestSignalFrequency::Hz1);
  // Enables the analog RLD loop for electrically isolated bench tests only.
  bool configureRld(bool enabled);

  // RDATA mode: start conversions, wait for dataReady(), then call readSample().
  bool startConversions();
  void stopConversions();
  bool dataReady() const;
  bool readSample(Sample& sample);
  bool calibrateOffset();

  // SupplyMeasurement requires Gain::X1 to avoid PGA saturation.
  // For InputMux::TestSignal, enable the source with configureTestSignal().

  // Lead-off is disabled after begin(). Enable it explicitly after checking
  // that the selected current complies with the applicable patient-safety rules.
  // readLeadOffStatus().valid is true only for configured DC detection.
  bool configureLeadOff(const LeadOffConfig& config);
  bool disableLeadOff();
  LeadOffStatus readLeadOffStatus();

  uint8_t readId();
  bool checkCommunication(uint8_t& id);

private:
  uint8_t transfer(uint8_t output);
  void sendCommand(uint8_t command);
  uint8_t readRegister(uint8_t address);
  bool readRegisterStable(uint8_t address, uint8_t& value);
  bool writeRegister(uint8_t address, uint8_t value);
  bool updateRegister(uint8_t address, uint8_t mask, uint8_t value);
};
