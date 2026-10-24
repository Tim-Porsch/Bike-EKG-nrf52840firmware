#include "../LogWriter.h"
// Hosttest mit In-Memory-SD: META-Header, signed Records, Footer und Altformat.
// Optionaler Ausgabepfad erzeugt eine BIN-Referenz für weitere Parserprüfungen.
#include "../SdFileProtocol.h"
#include <assert.h>
#include <fstream>
#include <string.h>

bool SdService::begin() { ++testMounts; return true; }
void SdService::end() { ++testUnmounts; }
const char* SdService::lastError() const { return "none"; }
bool SdService::openNextLogFile(FsFile& file, char* name, size_t size) {
  snprintf(name, size, "EKG%05u.BIN", unsigned(testFiles.size()));
  testFiles[name] = {};
  return file.open(name, O_RDONLY);
}
bool TimeService::hasUnixTime() const { return true; }
uint32_t TimeService::unixTime() const { return 1700000000; }
uint32_t TimeService::bootTimeUs() const { return 100000; }

int main(int argc, char** argv) {
  using namespace SdFileProtocol;
  SdService sd;
  TimeService time;
  LogWriter writer(sd);
  MeasurementSettings settings;
  settings.testDurationSeconds = 1;
  const char* participant = u8"P-Ä🫀";
  const char* comment = u8"Sitzend\nGriffe drücken";
  RecordingMetadata metadata;
  memcpy(metadata.bytes, "META", 4);
  put16(metadata.bytes + 4, 1);
  put16(metadata.bytes + 6, strlen(participant));
  put16(metadata.bytes + 8, strlen(comment));
  metadata.length = 12 + strlen(participant) + strlen(comment);
  memcpy(metadata.bytes + 12, participant, strlen(participant));
  memcpy(metadata.bytes + 12 + strlen(participant), comment, strlen(comment));
  assert(writer.begin(settings, 500, 8, time, "0.10.0", &metadata));
  const auto& headerFile = testFiles.at("EKG00000.BIN");
  assert(get16(headerFile.data() + 14) & RecordingMetadata::HEADER_FLAG);
  assert(!memcmp(headerFile.data() + 44, metadata.bytes, metadata.length));
  assert(writer.append({0, 1234, -8388608}));
  assert(writer.append({1, 3234, 8388607}));
  SampleRingBuffer::Snapshot snapshot = {};
  snapshot.samplesReceived = snapshot.samplesProcessed = 2;
  assert(writer.finish("user_stop", 1000000, snapshot));
  const auto& completed = testFiles.at("EKG00000.BIN");
  assert(completed.size() == 1048);
  assert(get16(completed.data() + 4) == 1 && get16(completed.data() + 6) == 512);
  assert(get32(completed.data() + 520) == uint32_t(-8388608));
  assert(!memcmp(completed.data() + 536, "END!", 4));
  RecordingMetadata decoded;
  assert(RecordingMetadata::fromHeader(completed.data(), 512, decoded) == 1);
  assert(decoded.length == metadata.length && !memcmp(decoded.bytes, metadata.bytes, metadata.length));
  if (argc > 1) {
    std::ofstream file(argv[1], std::ios::binary);
    file.write(reinterpret_cast<const char*>(completed.data()), completed.size());
    assert(file.good());
  }
  assert(writer.begin(settings, 500, 8, time, "0.10.0")); // Existing plain USB/NUS start remains compatible.
  const auto& legacy = testFiles.at("EKG00001.BIN");
  assert(!(get16(legacy.data() + 14) & RecordingMetadata::HEADER_FLAG));
  for (size_t i = 44; i < 512; ++i) assert(legacy[i] == 0);
  writer.abort();
  metadata.bytes[4] = 2;
  const size_t count = testFiles.size();
  assert(!writer.begin(settings, 500, 8, time, "0.10.0", &metadata));
  assert(testFiles.size() == count);
  return 0;
}
