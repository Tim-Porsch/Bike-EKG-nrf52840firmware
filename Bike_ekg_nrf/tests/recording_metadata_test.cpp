#include "../RecordingMetadata.h"
// UTF-8-/Layout-Grenzen und Kompatibilität zu Headern ohne META-Erweiterung prüfen.
// Testdaten sind synthetisch und enthalten keine echten Personenangaben.
#include <assert.h>
#include <string>
#include <vector>

using namespace SdFileProtocol;

static std::vector<uint8_t> encode(const std::string& participant, const std::string& comment) {
  std::vector<uint8_t> bytes(12 + participant.size() + comment.size());
  memcpy(bytes.data(), "META", 4);
  put16(bytes.data() + 4, 1);
  put16(bytes.data() + 6, participant.size());
  put16(bytes.data() + 8, comment.size());
  memcpy(bytes.data() + 12, participant.data(), participant.size());
  memcpy(bytes.data() + 12 + participant.size(), comment.data(), comment.size());
  return bytes;
}

int main() {
  const auto maximum = encode(std::string(80, 'A'), std::string(376, 'B'));
  assert(maximum.size() == 468 && RecordingMetadata::valid(maximum.data(), maximum.size()));
  const auto tooLong = encode(std::string(81, 'A'), "");
  assert(!RecordingMetadata::valid(tooLong.data(), tooLong.size()));
  const auto unicode = encode(u8"P-Ä🫀", u8"Ruhig\nGriffe drücken\t✓");
  assert(RecordingMetadata::valid(unicode.data(), unicode.size()));
  const auto badName = encode("P\n01", "okay");
  assert(!RecordingMetadata::valid(badName.data(), badName.size()));
  const auto badUtf8 = encode("P01", std::string("\xED\xA0\x80", 3));
  assert(!RecordingMetadata::valid(badUtf8.data(), badUtf8.size()));
  const auto overlong = encode("P01", std::string("\xC0\x80", 2));
  assert(!RecordingMetadata::valid(overlong.data(), overlong.size()));
  const auto incomplete = encode("P01", std::string("\xF0\x9F", 2));
  assert(!RecordingMetadata::valid(incomplete.data(), incomplete.size()));
  assert(!RecordingMetadata::valid(unicode.data(), unicode.size() - 1));
  uint8_t header[512] = {};
  memcpy(header, "BEKG", 4); put16(header + 4, 1); put16(header + 6, 512); put16(header + 8, 12);
  RecordingMetadata metadata;
  assert(RecordingMetadata::fromHeader(header, sizeof(header), metadata) == 0);
  put16(header + 14, 4);
  memcpy(header + 44, unicode.data(), unicode.size());
  assert(RecordingMetadata::fromHeader(header, sizeof(header), metadata) == 1);
  assert(metadata.length == unicode.size());
  assert(!memcmp(metadata.bytes, unicode.data(), unicode.size()));
  header[48] = 2;
  assert(RecordingMetadata::fromHeader(header, sizeof(header), metadata) == 2 && metadata.length == 0);
  header[48] = 1;
  assert(RecordingMetadata::fromHeader(header, 511, metadata) == 2);
  return 0;
}
