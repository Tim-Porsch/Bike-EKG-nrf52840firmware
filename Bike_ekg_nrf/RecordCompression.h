#pragma once
#include "SdFileProtocol.h"
#include <string.h>

// Independent block: first <III> record verbatim, then three modular uint32
// deltas per record, ZigZag + unsigned LEB128. ADC bits are preserved exactly.
namespace RecordCompression {
// Verlustfreie Transportkompression, keine Änderung des BEKG-Speicherformats.
// consumed zählt Originalbytes, der Rückgabewert belegte Ausgabebytes.
constexpr uint16_t MAX_RAW = 384;
inline uint16_t encode(const uint8_t* raw, uint16_t size, uint8_t* output,
                       uint16_t capacity, uint16_t& consumed) {
  consumed = 0;
  if (size < 12 || capacity < 12) return 0;
  memcpy(output, raw, 12);
  uint16_t written = 12;
  consumed = 12;
  while (consumed + 12 <= size) {
    uint8_t encoded[15];
    uint8_t length = 0;
    for (uint8_t field = 0; field < 12; field += 4) {
      // Modulo-2^32-Differenzen erhalten Überläufe und die ADC-Bitmuster exakt.
      const uint32_t delta = SdFileProtocol::get32(raw + consumed + field) -
                             SdFileProtocol::get32(raw + consumed - 12 + field);
      uint32_t value = (delta << 1) ^ (0u - (delta >> 31));
      do {
        encoded[length++] = (value & 127) | (value >= 128 ? 128 : 0);
        value >>= 7;
      } while (value);
    }
    if (written + length > capacity) break;
    memcpy(output + written, encoded, length);
    written += length;
    consumed += 12;
  }
  return written;
}
}
