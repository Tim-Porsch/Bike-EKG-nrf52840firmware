#pragma once

// BEKG-v1-Rohdaten: 512 Byte Header, 12 Byte je Record, 512 Byte Footer.
// Strukturlayout und Größen sind Teil des Dateiformats; Parser mitpflegen.

#include "SampleRingBuffer.h"
#include "SdService.h"
#include "SettingsService.h"
#include "TimeService.h"
#include "RecordingMetadata.h"

class LogWriter {
public:
  explicit LogWriter(SdService& sdService);

  bool begin(const MeasurementSettings& settings, uint16_t sampleRateSps,
             uint8_t gainMultiplier, const TimeService& timeService,
              const char* firmwareVersion, const RecordingMetadata* metadata = nullptr);
  bool append(const SampleRingBuffer::Record& record);
  bool finish(const char* reason, uint32_t durationUs,
              const SampleRingBuffer::Snapshot& snapshot);
  void abort();

  bool isOpen() const;
  const char* fileName() const;
  const char* lastError() const;

private:
  static constexpr uint16_t FORMAT_VERSION = 1;
  static constexpr size_t HEADER_BYTES = 512;
  static constexpr size_t FOOTER_BYTES = 512;
  static constexpr size_t RECORDS_PER_BLOCK = 128;

  struct Header {
    char magic[4];
    uint16_t formatVersion;
    uint16_t headerBytes;
    uint16_t recordBytes;
    uint16_t sampleRateSps;
    uint8_t gainMultiplier;
    uint8_t inputMux;
    uint16_t flags;
    uint32_t unixTime;
    uint32_t bootTimeUs;
    uint32_t expectedDurationSeconds;
    char firmwareVersion[16];
    uint8_t reserved[468];
  };
  static_assert(sizeof(Header) == HEADER_BYTES, "Unexpected log header size");

  struct Footer {
    char magic[4];
    uint16_t formatVersion;
    uint16_t footerBytes;
    uint32_t durationUs;
    uint32_t samplesWritten;
    uint32_t samplesReceived;
    uint32_t samplesProcessed;
    uint32_t samplesDropped;
    uint32_t sampleReadErrors;
    uint32_t inferredMissedDrdy;
    uint32_t settlingSamplesDiscarded;
    uint16_t ringMaxFill;
    uint16_t reserved;
    char reason[16];
    char error[64];
    uint8_t padding[388];
  };
  static_assert(sizeof(Footer) == FOOTER_BYTES, "Unexpected log footer size");

  SdService& sdService_;
  FsFile file_;
  SampleRingBuffer::Record records_[RECORDS_PER_BLOCK];
  char fileName_[13] = {};
  const char* lastError_ = "not started";
  uint16_t bufferedRecords_ = 0;
  uint32_t samplesWritten_ = 0;
  bool open_ = false;
  bool writeFailed_ = false;

  bool flush();
  bool writeExact(const void* data, size_t length);
  void setError(const char* error);
  static void copyText(char* destination, size_t destinationSize,
                       const char* source);
};
