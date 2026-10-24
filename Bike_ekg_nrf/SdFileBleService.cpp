#include "SdFileBleService.h"
#include <string.h>

using namespace SdFileProtocol;
SdFileBleService* SdFileBleService::instance_ = nullptr;

SdFileBleService::SdFileBleService(SdService& sd)
    : sd_(sd), service_("7f530001-1212-efde-1523-785feabcd123"),
      control_("7f530002-1212-efde-1523-785feabcd123"),
      response_("7f530003-1212-efde-1523-785feabcd123"),
      data_("7f530004-1212-efde-1523-785feabcd123") {}

void SdFileBleService::begin() {
  instance_ = this;
  service_.begin();
  control_.setProperties(CHR_PROPS_WRITE);
  control_.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  control_.setMaxLen(20);
  control_.setWriteCallback(receive, false);
  control_.begin();
  response_.setProperties(CHR_PROPS_READ);
  response_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  response_.setMaxLen(MAX_RESPONSE);
  response_.begin();
  data_.setProperties(CHR_PROPS_NOTIFY);
  data_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  data_.setMaxLen(STREAM_MAX_BYTES);
  data_.begin();
  reply(Done);
}

void SdFileBleService::receive(uint16_t connection, BLECharacteristic*, uint8_t* data, uint16_t length) {
  SdFileBleService* self = instance_;
  if (!self || connection != Bluefruit.connHandle() || length < 6 || length > 20) return;
  // BLE task only copies a bounded command. Filesystem work runs in loop().
  taskENTER_CRITICAL();
  if (self->requestPending_) { taskEXIT_CRITICAL(); return; }
  memcpy(self->incoming_, data, length);
  self->incomingLength_ = length;
  taskEXIT_CRITICAL();
  // Invalidate the readable value BEFORE acknowledging this write. A new page
  // may reuse an old request ID after reconnect: it must not accept old data.
  const uint8_t pending[HEADER] = {0xF2, 1}; // operation 0 never matches a request
  self->response_.write(pending, sizeof(pending));
  taskENTER_CRITICAL();
  self->requestPending_ = true;
  taskEXIT_CRITICAL();
}

void SdFileBleService::disconnected() { disconnectPending_ = true; }
bool SdFileBleService::active() const { return active_ || metadataSession_; }

bool SdFileBleService::consumeMetadata(uint32_t token, RecordingMetadata& metadata) {
  // Ein versiegelter Textsatz darf genau einen Aufzeichnungsstart autorisieren.
  // Token bindet an die Sitzung, ist aber kein kryptografischer Zugriffsschutz.
  if (!metadataSession_ || !metadataSealed_ || !token || token != metadataToken_ ||
      disconnectPending_ || !Bluefruit.Periph.connected() || millis() - lastActivityMs_ > 15000) return false;
  metadata = metadata_;
  close(); // Single use; never leak these values to another recording.
  return true;
}

void SdFileBleService::close() {
  streaming_ = streamFinished_ = false;
  streamCompressed_ = false;
  streamPacketLength_ = 0;
  streamSession_ = 0;
  listing_ = false;
  newestFirst_ = indexing_ = false;
  nextNumber_ = 0;
  file_.close();
  directory_.close();
  if (active_) sd_.end();
  active_ = false;
  metadataSession_ = metadataSealed_ = false;
  metadataExpected_ = metadataReceived_ = 0;
  metadataToken_ = 0;
  metadata_.clear();
}

uint16_t SdFileBleService::streamMtu() {
  BLEConnection* connection = Bluefruit.Connection(Bluefruit.connHandle());
  return connection ? connection->getMtu() : 23;
}

void SdFileBleService::pumpStream() {
  // Fenster und Offsets beziehen sich immer auf rekonstruierte Originaldateibytes,
  // auch wenn auf der Funkstrecke komprimierte Pakete verwendet werden.
  if (!streaming_ || disconnectPending_ || streamSent_ >= streamSize_) return;
  if (!data_.notifyEnabled()) { error("SD download subscriber disconnected"); return; }
  const uint32_t available = STREAM_WINDOW - (streamSent_ - streamAcked_);
  if (!available) return;
  if (!streamPacketLength_) {
    const uint16_t maxBytes = min(uint16_t(STREAM_MAX_BYTES), uint16_t(streamMtu() - 3));
    uint16_t count = min(uint32_t(maxBytes - STREAM_HEADER), min(available, streamSize_ - streamSent_));
    bool compressed = false;
    const bool records = streamCompressed_ && maxBytes >= 29 && streamSize_ >= 1024 && (streamSize_ - 1024) % 12 == 0;
    const uint32_t dataEnd = records ? streamSize_ - 512 : 0;
    if (records && streamSent_ < 512) count = min(uint32_t(count), 512 - streamSent_);
    if (records && streamSent_ >= 512 && streamSent_ < dataEnd) {
      const uint16_t misalignment = (streamSent_ - 512) % 12;
      if (misalignment) count = min(uint16_t(count), uint16_t(12 - misalignment));
      else if (maxBytes >= 29 && available >= 12) {
        count = min(uint32_t(RecordCompression::MAX_RAW), min(available, dataEnd - streamSent_));
        count -= count % 12;
        compressed = true;
      } else count = min(uint32_t(count), dataEnd - streamSent_);
    }
    if (file_.read(streamRaw_, count) != count) { error("SD stream read failed"); return; }
    uint16_t encoded = 0, consumed = 0;
    if (compressed) {
      encoded = RecordCompression::encode(streamRaw_, count, streamPacket_ + 14, maxBytes - 14, consumed);
      // Include the two-byte decoded length in the compression cost. Never
      // expand a block; use the original raw frame when compression loses.
      compressed = consumed > encoded + 2;
      if (!compressed) consumed = min(count, uint16_t(maxBytes - STREAM_HEADER));
      if (consumed != count && !file_.seekSet(streamSent_ + consumed)) { error("Cannot position compressed SD stream"); return; }
      count = consumed;
    }
    // Hash each source byte once. Retransmissions may rewind the file but must
    // not duplicate data in the whole-file hash.
    if (streamSent_ + count > streamHashed_) {
      const uint32_t already = streamHashed_ - streamSent_;
      streamHash_.update(streamRaw_ + already, count - already);
      streamHashed_ = streamSent_ + count;
    }
    streamPacket_[0] = 0xF3; streamPacket_[1] = compressed ? 2 : 1;
    put16(streamPacket_ + 2, streamSession_);
    put32(streamPacket_ + 4, streamSent_);
    if (compressed) put16(streamPacket_ + 12, count);
    else memcpy(streamPacket_ + STREAM_HEADER, streamRaw_, count);
    streamPacketLength_ = compressed ? 14 + encoded : STREAM_HEADER + count;
    streamPacketRawLength_ = count;
    const uint32_t crc = crc32Update(crc32Update(0xFFFFFFFF, streamPacket_, 8), streamPacket_ + STREAM_HEADER, streamPacketLength_ - STREAM_HEADER);
    put32(streamPacket_ + 8, ~crc);
  }
  // At most one notification per update; the window bounds all in-flight data.
  // Bluefruit manages TX credits internally and may wait for radio capacity.
  if (data_.notify(streamPacket_, streamPacketLength_)) {
    streamSent_ += streamPacketRawLength_;
    streamPacketLength_ = 0;
  }
}

void SdFileBleService::reply(uint8_t status, uint32_t value, uint16_t length) {
  if (disconnectPending_) return; // Do not publish old-session filesystem work.
  bytes_[0] = 0xF2;
  bytes_[1] = 1;
  put16(bytes_ + 2, get16(request_ + 2));
  bytes_[4] = request_[4];
  bytes_[5] = status;
  put16(bytes_ + 6, length);
  put32(bytes_ + 8, value);
  put32(bytes_ + 12, crc32(bytes_ + HEADER, length));
  response_.write(bytes_, HEADER + length);
}

void SdFileBleService::error(const char* message) {
  close();
  const size_t length = min(strlen(message), size_t(MAX_PAYLOAD));
  memcpy(bytes_ + HEADER, message, length);
  reply(Error, 0, length);
}

void SdFileBleService::nextEntry() {
  if (newestFirst_) { nextNewestEntry(); return; }
  // Bound scanning work even if many unrelated files/directories exist.
  for (uint8_t i = 0; i < 16; ++i) {
    FsFile entry;
    if (!entry.openNext(&directory_, O_RDONLY)) {
      if (directory_.getError()) { error("SD directory read failed"); return; }
      close();
      reply(Done);
      return;
    }
    char name[32] = {};
    entry.getName(name, sizeof(name));
    if (!entry.isFile() || !logName(name, strlen(name))) { entry.close(); continue; }
    replyEntry(entry, name);
    return;
  }
}

void SdFileBleService::nextNewestEntry() {
  // Read only directory names first, not every recording header. Keep each loop bounded.
  for (uint8_t i = 0; indexing_ && i < 16; ++i) {
    FsFile entry;
    if (!entry.openNext(&directory_, O_RDONLY)) {
      if (directory_.getError()) { error("SD directory read failed"); return; }
      indexing_ = false;
      break;
    }
    char name[32] = {};
    entry.getName(name, sizeof(name));
    if (entry.isFile() && logName(name, strlen(name))) {
      uint32_t number = 0;
      for (uint8_t digit = 3; digit < 8; ++digit) number = number * 10 + uint32_t(name[digit] - '0');
      recordingNames_[number / 8] |= uint8_t(1U << (number % 8));
      if (number >= nextNumber_) nextNumber_ = number + 1;
    }
    entry.close();
  }
  if (indexing_) return;
  while (nextNumber_) {
    const uint32_t number = --nextNumber_;
    if (!(recordingNames_[number / 8] & uint8_t(1U << (number % 8)))) continue;
    char name[] = "EKG00000.BIN";
    uint32_t digits = number;
    for (int8_t position = 7; position >= 3; --position) {
      name[position] = char('0' + digits % 10);
      digits /= 10;
    }
    FsFile entry;
    if (!sd_.openRead(entry, name)) { error("Cannot open recording metadata"); return; }
    replyEntry(entry, name);
    return;
  }
  close();
  reply(Done);
}

void SdFileBleService::replyEntry(FsFile& entry, const char* name) {
  const uint64_t size = entry.fileSize();
  RecordingMetadata metadata;
  uint8_t metadataStatus = 0;
  if (listInfo_) {
    uint8_t header[512];
    const int count = entry.read(header, sizeof(header));
    metadataStatus = RecordingMetadata::fromHeader(header, count < 0 ? 0 : size_t(count), metadata);
  }
  entry.close();
  if (size > UINT32_MAX) { error("Recording exceeds 4 GiB"); return; }
  memcpy(bytes_ + HEADER, name, 12);
  uint16_t payloadLength = 12;
  if (listInfo_) {
    bytes_[HEADER + 12] = metadataStatus;
    if (metadataStatus == 1) memcpy(bytes_ + HEADER + 13, metadata.bytes, metadata.length);
    payloadLength += 1 + metadata.length;
  }
  listing_ = false;
  reply(Ok, static_cast<uint32_t>(size), payloadLength);
}

void SdFileBleService::update(bool allowed) {
  // Einziger Ausführungskontext für SD-Operationen; allowed kommt aus dem
  // Messzustandsautomaten und verhindert Konkurrenz zur laufenden Aufzeichnung.
  if (disconnectPending_ || !Bluefruit.Periph.connected()) {
    close();
    taskENTER_CRITICAL();
    disconnectPending_ = requestPending_ = false;
    taskEXIT_CRITICAL();
    hasLastRequest_ = false;
    return;
  }
  if (active() && millis() - lastActivityMs_ > 15000) {
    error("SD session timed out; reload file list");
    hasLastRequest_ = false;
  }
  uint8_t incoming[20];
  uint16_t length = 0;
  taskENTER_CRITICAL();
  if (requestPending_) {
    length = incomingLength_;
    memcpy(incoming, incoming_, length);
    requestPending_ = false;
  }
  taskEXIT_CRITICAL();
  if (length) {
    lastActivityMs_ = millis();
    // Retrying a request must not advance the directory a second time.
    if (hasLastRequest_ && length == requestLength_ && !memcmp(incoming, request_, length)) {
      if (listing_) nextEntry();
      else response_.write(bytes_, HEADER + get16(bytes_ + 6));
      return;
    }
    memcpy(request_, incoming, length);
    requestLength_ = length;
    hasLastRequest_ = true;
    listing_ = false;
    if (request_[0] != 0xF1 || request_[1] != 1 || request_[5] != 0) {
      error("Invalid SD request"); return;
    }
    const uint8_t operation = request_[4];
    if (operation == Close && length == 6) { close(); reply(Ok); return; }
    if (!allowed) { error("SD downloads require idle mode"); return; }
    if (operation == MetadataBegin && length == 8) {
      close();
      metadataExpected_ = get16(request_ + 6);
      if (metadataExpected_ < RecordingMetadata::PREFIX || metadataExpected_ > RecordingMetadata::CAPACITY) {
        error("Invalid metadata length"); return;
      }
      metadataSession_ = true;
      metadataToken_ = ++nextMetadataToken_;
      if (!metadataToken_) metadataToken_ = ++nextMetadataToken_;
      reply(Ok, metadataToken_);
    } else if (operation == MetadataChunk && length >= 9 && metadataSession_ && !metadataSealed_) {
      const uint16_t offset = get16(request_ + 6);
      const uint16_t count = length - 8;
      if (offset != metadataReceived_ || offset + count > metadataExpected_) { error("Invalid metadata offset"); return; }
      memcpy(metadata_.bytes + offset, request_ + 8, count);
      metadataReceived_ += count;
      reply(Ok, metadataReceived_);
    } else if (operation == MetadataSeal && length == 10 && metadataSession_ && !metadataSealed_) {
      if (metadataReceived_ != metadataExpected_ || get32(request_ + 6) != crc32(metadata_.bytes, metadataExpected_) ||
          !RecordingMetadata::valid(metadata_.bytes, metadataExpected_)) { error("Invalid recording metadata or checksum"); return; }
      metadata_.length = metadataExpected_;
      metadataSealed_ = true;
      reply(Ok, metadataToken_);
    } else if ((operation == List || operation == ListInfo || operation == ListInfoNewest) && length == 6) {
      close();
      listInfo_ = operation != List;
      newestFirst_ = indexing_ = operation == ListInfoNewest;
      if (newestFirst_) memset(recordingNames_, 0, sizeof(recordingNames_));
      if (!sd_.begin()) { error(sd_.lastError()); return; }
      active_ = true;
      if (!sd_.openRoot(directory_)) { error("Cannot open SD directory"); return; }
      listing_ = true;
    } else if ((operation == Next || operation == NextInfo) && length == 6 && directory_.isOpen() && listInfo_ == (operation == NextInfo)) {
      listing_ = true;
    } else if (operation == Open && length == 18 && logName(reinterpret_cast<char*>(request_ + 6), 12)) {
      close();
      if (!sd_.begin()) { error(sd_.lastError()); return; }
      active_ = true;
      char name[13] = {};
      memcpy(name, request_ + 6, 12);
      if (!sd_.openRead(file_, name)) { error("Cannot open recording"); return; }
      const uint64_t size = file_.fileSize();
      if (size > UINT32_MAX) { error("Recording exceeds 4 GiB"); return; }
      reply(Ok, static_cast<uint32_t>(size));
    } else if ((operation == StreamStart || operation == StreamStartCompressed) && length == 6 && file_.isOpen() && data_.notifyEnabled()) {
      if (!file_.seekSet(0)) { error("Cannot rewind SD recording"); return; }
      streaming_ = true;
      streamCompressed_ = operation == StreamStartCompressed;
      streamFinished_ = false;
      streamSession_ = ++nextStreamSession_;
      if (!streamSession_) streamSession_ = ++nextStreamSession_;
      streamSize_ = static_cast<uint32_t>(file_.fileSize());
      streamSent_ = streamAcked_ = streamHashed_ = 0;
      streamPacketLength_ = 0;
      streamHash_.reset();
      put32(bytes_ + HEADER, streamSize_);
      put16(bytes_ + HEADER + 4, STREAM_WINDOW);
      put16(bytes_ + HEADER + 6, streamMtu());
      reply(Ok, streamSession_, 8);
    } else if ((operation == StreamAck || operation == StreamSeek) && length == 12 && streaming_) {
      const uint16_t session = get16(request_ + 6);
      const uint32_t offset = get32(request_ + 8);
      if (session != streamSession_ || offset < streamAcked_ || offset > streamSent_) {
        error("Invalid SD stream acknowledgement"); return;
      }
      streamAcked_ = offset;
      if (operation == StreamSeek) {
        if (!file_.seekSet(offset)) { error("Cannot retransmit SD recording"); return; }
        streamSent_ = offset;
        streamPacketLength_ = 0;
      }
      reply(Ok, offset);
    } else if (operation == StreamFinish && length == 8 && file_.isOpen() && streamSession_ &&
               (streaming_ || streamFinished_) && get16(request_ + 6) == streamSession_) {
      if (streamAcked_ != streamSize_ || streamHashed_ != streamSize_) {
        error("SD download not complete"); return;
      }
      streamHash_.digest(bytes_ + HEADER);
      streaming_ = false;
      streamFinished_ = true;
      reply(Ok, streamSize_, 32);
    } else if (operation == Read && length == 10 && file_.isOpen() && !streaming_) {
      const uint32_t offset = get32(request_ + 6);
      const uint32_t size = static_cast<uint32_t>(file_.fileSize());
      if (offset > size || !file_.seekSet(offset)) { error("Invalid file offset"); return; }
      const uint16_t count = min(uint32_t(MAX_PAYLOAD), size - offset);
      const int read = file_.read(bytes_ + HEADER, count);
      if (read != count) { error("SD read failed"); return; }
      reply(offset == size ? Done : Ok, offset, count);
    } else {
      error("Invalid SD operation or session");
    }
  }
  if (listing_) nextEntry();
  pumpStream();
}
