#include "LogWriter.h"

#include <string.h>

LogWriter::LogWriter(SdService& sdService) : sdService_(sdService) {}

bool LogWriter::begin(const MeasurementSettings& settings,
                      uint16_t sampleRateSps, uint8_t gainMultiplier,
                      const TimeService& timeService,
                       const char* firmwareVersion, const RecordingMetadata* metadata) {
  if (open_) {
    setError("log file is already open");
    return false;
  }
  if (metadata && !RecordingMetadata::valid(metadata->bytes, metadata->length)) {
    setError("invalid recording metadata");
    return false;
  }
  if (!sdService_.begin()) {
    setError(sdService_.lastError());
    return false;
  }
  if (!sdService_.openNextLogFile(file_, fileName_, sizeof(fileName_))) {
    setError(sdService_.lastError());
    sdService_.end();
    return false;
  }

  // Speicher vor der Erfassung reservieren, um Dateisystemarbeit im Messpfad
  // zu reduzieren. Beim Abschluss auf die tatsächlich geschriebene Größe kürzen.
  const uint64_t expectedRecords =
      static_cast<uint64_t>(sampleRateSps) * settings.testDurationSeconds;
  const uint64_t expectedBytes =
      HEADER_BYTES + expectedRecords * sizeof(SampleRingBuffer::Record) +
      FOOTER_BYTES;
  if (!file_.preAllocate(expectedBytes) || !file_.seekSet(0)) {
    setError("could not reserve log file space");
    abort();
    return false;
  }

  Header header = {};
  memcpy(header.magic, "BEKG", sizeof(header.magic));
  header.formatVersion = FORMAT_VERSION;
  header.headerBytes = HEADER_BYTES;
  header.recordBytes = sizeof(SampleRingBuffer::Record);
  header.sampleRateSps = sampleRateSps;
  header.gainMultiplier = gainMultiplier;
  header.inputMux = static_cast<uint8_t>(settings.input);
  header.flags = (settings.rldEnabled ? 1 : 0) |
                 (timeService.hasUnixTime() ? 2 : 0) |
                 (metadata ? RecordingMetadata::HEADER_FLAG : 0);
  header.unixTime = timeService.unixTime();
  header.bootTimeUs = timeService.bootTimeUs();
  header.expectedDurationSeconds = settings.testDurationSeconds;
  copyText(header.firmwareVersion, sizeof(header.firmwareVersion),
            firmwareVersion);
  if (metadata) memcpy(header.reserved, metadata->bytes, metadata->length);

  if (!writeExact(&header, sizeof(header)) || !file_.sync()) {
    setError("could not write log header");
    abort();
    return false;
  }

  bufferedRecords_ = 0;
  samplesWritten_ = 0;
  writeFailed_ = false;
  open_ = true;
  lastError_ = "none";
  return true;
}

bool LogWriter::append(const SampleRingBuffer::Record& record) {
  // Ungefilterte Records blockweise sammeln; kein Dateizugriff aus der ADC-ISR.
  if (!open_) {
    setError("log file is not open");
    return false;
  }
  if (writeFailed_) {
    return false;
  }

  records_[bufferedRecords_++] = record;
  if (bufferedRecords_ == RECORDS_PER_BLOCK) {
    if (!flush()) {
      writeFailed_ = true;
      return false;
    }
  }
  return true;
}

bool LogWriter::finish(const char* reason, uint32_t durationUs,
                       const SampleRingBuffer::Snapshot& snapshot) {
  if (!open_) {
    return false;
  }

  // Nur vollständig geschriebene Nutzdaten erhalten einen regulären END!-Footer.
  // Nach Schreibfehlern darf die Datei nicht als erfolgreiche Messung erscheinen.
  bool success = !writeFailed_ && flush();
  if (success) {
    Footer footer = {};
    memcpy(footer.magic, "END!", sizeof(footer.magic));
    footer.formatVersion = FORMAT_VERSION;
    footer.footerBytes = FOOTER_BYTES;
    footer.durationUs = durationUs;
    footer.samplesWritten = samplesWritten_;
    footer.samplesReceived = snapshot.samplesReceived;
    footer.samplesProcessed = snapshot.samplesProcessed;
    footer.samplesDropped = snapshot.samplesDropped;
    footer.sampleReadErrors = snapshot.sampleReadErrors;
    footer.inferredMissedDrdy = snapshot.inferredMissedDrdy;
    footer.settlingSamplesDiscarded = snapshot.settlingSamplesDiscarded;
    footer.ringMaxFill = snapshot.maxFill;
    copyText(footer.reason, sizeof(footer.reason), reason);
    copyText(footer.error, sizeof(footer.error), "none");
    success = writeExact(&footer, sizeof(footer));
    if (!success) {
      setError("could not write log footer");
    }
  }
  if (success && !file_.truncate()) {
    setError("could not finalize log file");
    success = false;
  }
  if (success && !file_.sync()) {
    setError("could not sync log file");
    success = false;
  }
  if (!file_.close()) {
    setError("could not close log file");
    success = false;
  }

  sdService_.end();
  open_ = false;
  writeFailed_ = false;
  return success;
}

void LogWriter::abort() {
  if (file_) {
    file_.close();
  }
  sdService_.end();
  open_ = false;
  bufferedRecords_ = 0;
  writeFailed_ = false;
}

bool LogWriter::isOpen() const {
  return open_;
}

const char* LogWriter::fileName() const {
  return fileName_;
}

const char* LogWriter::lastError() const {
  return lastError_;
}

bool LogWriter::flush() {
  if (bufferedRecords_ == 0) {
    return true;
  }

  const size_t bytes = bufferedRecords_ * sizeof(records_[0]);
  if (!writeExact(records_, bytes)) {
    setError("could not write sample block");
    return false;
  }

  samplesWritten_ += bufferedRecords_;
  bufferedRecords_ = 0;
  return true;
}

bool LogWriter::writeExact(const void* data, size_t length) {
  return file_.write(data, length) == length;
}

void LogWriter::setError(const char* error) {
  lastError_ = error;
}

void LogWriter::copyText(char* destination, size_t destinationSize,
                         const char* source) {
  if (destinationSize == 0) {
    return;
  }

  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}
