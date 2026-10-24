#pragma once

#include <stdint.h>
#include <stddef.h>

// Pull protocol: one write request, then one stable, readable response.
namespace SdFileProtocol {
// Gemeinsamer Wire-Vertrag mit files.js: Little Endian und feste Operationscodes.
// CRC32 erkennt Übertragungsfehler, authentifiziert aber weder Client noch Gerät.
constexpr uint16_t MAX_RESPONSE = 512;
constexpr uint16_t HEADER = 16;
constexpr uint16_t MAX_PAYLOAD = MAX_RESPONSE - HEADER;
enum Operation : uint8_t {
  List = 1, Next = 2, Open = 3, Read = 4, Close = 5,
  MetadataBegin = 6, MetadataChunk = 7, MetadataSeal = 8,
  ListInfo = 9, NextInfo = 10,
  StreamStart = 11, StreamAck = 12, StreamSeek = 13, StreamFinish = 14,
  StreamStartCompressed = 15,
  ListInfoNewest = 16,
};
enum Status : uint8_t { Ok = 0, Done = 1, Error = 2 };

inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
inline void put32(uint8_t* p, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
inline uint32_t crc32Update(uint32_t crc, const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
  }
  return crc;
}
inline uint32_t crc32(const uint8_t* bytes, size_t length) { return ~crc32Update(0xFFFFFFFF, bytes, length); }
inline bool logName(const char* name, size_t length) {
  if (length != 12 || name[0] != 'E' || name[1] != 'K' || name[2] != 'G' ||
      name[8] != '.' || name[9] != 'B' || name[10] != 'I' || name[11] != 'N') return false;
  for (uint8_t i = 3; i < 8; ++i) if (name[i] < '0' || name[i] > '9') return false;
  return true;
}
} // namespace SdFileProtocol
