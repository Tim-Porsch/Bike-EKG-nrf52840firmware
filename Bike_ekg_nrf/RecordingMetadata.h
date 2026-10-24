#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "SdFileProtocol.h"

// Extension inside BEKG v1's previously zero-filled reserved header area.
// META, version:u16, participantBytes:u16, commentBytes:u16, reserved:u16,
// followed by the two UTF-8 strings (length-delimited, no terminators).
class RecordingMetadata {
  // Längen sind UTF-8-Bytes, nicht Zeichen. Keine dynamische Speicherbelegung.
public:
  static constexpr uint16_t CAPACITY = 468;
  static constexpr uint16_t PREFIX = 12;
  static constexpr uint16_t PARTICIPANT_MAX = 80;
  static constexpr uint16_t COMMENT_MAX = 376;
  static constexpr uint16_t HEADER_FLAG = 4;
  uint8_t bytes[CAPACITY] = {};
  uint16_t length = 0;

  void clear() { memset(bytes, 0, sizeof(bytes)); length = 0; }

  static bool utf8(const uint8_t* text, uint16_t length, bool multiline) {
    for (uint16_t i = 0; i < length;) {
      const uint8_t first = text[i++];
      if (first < 0x80) {
        if (first < 32 && !(multiline && (first == '\n' || first == '\r' || first == '\t'))) return false;
        if (first == 127) return false;
        continue;
      }
      uint8_t remaining;
      uint32_t value;
      uint32_t minimum;
      if (first >= 0xC2 && first <= 0xDF) { remaining = 1; value = first & 31; minimum = 0x80; }
      else if (first >= 0xE0 && first <= 0xEF) { remaining = 2; value = first & 15; minimum = 0x800; }
      else if (first >= 0xF0 && first <= 0xF4) { remaining = 3; value = first & 7; minimum = 0x10000; }
      else return false;
      if (i + remaining > length) return false;
      while (remaining--) {
        const uint8_t next = text[i++];
        if ((next & 0xC0) != 0x80) return false;
        value = (value << 6) | (next & 63);
      }
      if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
  }

  static bool valid(const uint8_t* data, size_t size) {
    // Erst Grenzen/Layout prüfen, danach auf die beiden Textbereiche zugreifen.
    if (size < PREFIX || size > CAPACITY || memcmp(data, "META", 4) ||
        SdFileProtocol::get16(data + 4) != 1 || SdFileProtocol::get16(data + 10) != 0) return false;
    const uint16_t participant = SdFileProtocol::get16(data + 6);
    const uint16_t comment = SdFileProtocol::get16(data + 8);
    return participant <= PARTICIPANT_MAX && comment <= COMMENT_MAX &&
           size == size_t(PREFIX) + participant + comment &&
           utf8(data + PREFIX, participant, false) && utf8(data + PREFIX + participant, comment, true);
  }

  // 0 = old header without metadata, 1 = valid metadata, 2 = unreadable/unsupported.
  static uint8_t fromHeader(const uint8_t* header, size_t size, RecordingMetadata& output) {
    output.clear();
    if (size != 512 || memcmp(header, "BEKG", 4) || SdFileProtocol::get16(header + 4) != 1 ||
        SdFileProtocol::get16(header + 6) != 512 || SdFileProtocol::get16(header + 8) != 12) return 2;
    if (!(SdFileProtocol::get16(header + 14) & HEADER_FLAG)) return 0;
    const uint8_t* extension = header + 44;
    const size_t length = PREFIX + size_t(SdFileProtocol::get16(extension + 6)) + SdFileProtocol::get16(extension + 8);
    if (!valid(extension, length)) return 2;
    memcpy(output.bytes, extension, length);
    output.length = length;
    return 1;
  }
};
