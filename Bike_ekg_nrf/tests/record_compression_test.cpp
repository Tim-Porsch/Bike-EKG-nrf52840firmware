#include "../RecordCompression.h"
// Feste Referenzbytes sichern ZigZag/LEB128-Vertrag einschließlich uint32-Überläufen.
// consumed und Ausgabelänge getrennt auf die jeweiligen Puffergrenzen prüfen.
#include <assert.h>
#include <string.h>

int main() {
  uint8_t raw[384] = {}, encoded[244] = {};
  uint16_t consumed = 99;
  assert(RecordCompression::encode(raw, 11, encoded, 244, consumed) == 0 && consumed == 0);
  assert(RecordCompression::encode(raw, 24, encoded, 11, consumed) == 0 && consumed == 0);
  assert(RecordCompression::encode(raw, 24, encoded, 12, consumed) == 12 && consumed == 12);
  SdFileProtocol::put32(raw + 12, 1);
  SdFileProtocol::put32(raw + 16, 2000);
  SdFileProtocol::put32(raw + 20, uint32_t(-1));
  assert(RecordCompression::encode(raw, 24, encoded, 230, consumed) == 16 && consumed == 24);
  const uint8_t expected[16] = {0,0,0,0,0,0,0,0,0,0,0,0,2,0xA0,0x1F,1};
  assert(!memcmp(encoded, expected, sizeof(expected)));
  SdFileProtocol::put32(raw + 12, 0x80000000);
  SdFileProtocol::put32(raw + 16, 0x7FFFFFFF);
  SdFileProtocol::put32(raw + 20, 0xFFFFFFFF);
  assert(RecordCompression::encode(raw, 24, encoded, 230, consumed) == 23 && consumed == 24);
  const uint8_t extremes[11] = {0xFF,0xFF,0xFF,0xFF,0x0F,0xFE,0xFF,0xFF,0xFF,0x0F,1};
  assert(!memcmp(encoded + 12, extremes, 11));
  memset(raw, 0, sizeof(raw));
  assert(RecordCompression::encode(raw, sizeof(raw), encoded, 230, consumed) == 105 && consumed == 384);
  for (uint16_t capacity = 0; capacity < 244; ++capacity) {
    const uint16_t length = RecordCompression::encode(raw, sizeof(raw), encoded, capacity, consumed);
    assert(length <= capacity && consumed <= 384 && consumed % 12 == 0);
  }
}
