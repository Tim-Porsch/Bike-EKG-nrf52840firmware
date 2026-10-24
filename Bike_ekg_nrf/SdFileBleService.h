#pragma once

// Exklusive SD-Lesesitzung bzw. vorbereitende Metadatenübertragung im Leerlauf.
// BLE-Callbacks reichen nur Befehle weiter; update() erledigt Dateisystemarbeit.

#include <bluefruit.h>
#include "SdService.h"
#include "SdFileProtocol.h"
#include "RecordingMetadata.h"
#include "Sha256.h"
#include "RecordCompression.h"

class SdFileBleService {
public:
  explicit SdFileBleService(SdService& sd);
  void begin();
  void update(bool allowed);
  void disconnected();
  bool active() const;
  bool consumeMetadata(uint32_t token, RecordingMetadata& metadata);

private:
  SdService& sd_;
  BLEService service_;
  BLECharacteristic control_;
  BLECharacteristic response_;
  BLECharacteristic data_;
  FsFile directory_;
  FsFile file_;
  bool active_ = false;
  bool listing_ = false;
  bool listInfo_ = false;
  bool newestFirst_ = false;
  bool indexing_ = false;
  uint32_t nextNumber_ = 0;
  // Fixed 12.5 KiB index for the complete EKG00000..EKG99999 namespace.
  uint8_t recordingNames_[12500] = {};
  bool metadataSession_ = false;
  bool metadataSealed_ = false;
  uint16_t metadataExpected_ = 0;
  uint16_t metadataReceived_ = 0;
  uint32_t metadataToken_ = 0;
  uint32_t nextMetadataToken_ = 0;
  RecordingMetadata metadata_;
  bool hasLastRequest_ = false;
  volatile bool disconnectPending_ = false;
  volatile bool requestPending_ = false;
  uint8_t incoming_[20] = {};
  uint16_t incomingLength_ = 0;
  uint8_t request_[20] = {};
  uint16_t requestLength_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint8_t bytes_[SdFileProtocol::MAX_RESPONSE] = {};
  static constexpr uint32_t STREAM_WINDOW = 4096;
  static constexpr uint16_t STREAM_HEADER = 12;
  static constexpr uint16_t STREAM_MAX_BYTES = 244;
  bool streaming_ = false;
  bool streamCompressed_ = false;
  bool streamFinished_ = false;
  uint16_t streamSession_ = 0;
  uint16_t nextStreamSession_ = 0;
  uint32_t streamSize_ = 0;
  uint32_t streamSent_ = 0;
  uint32_t streamAcked_ = 0;
  uint32_t streamHashed_ = 0;
  uint8_t streamPacket_[STREAM_MAX_BYTES] = {};
  uint16_t streamPacketLength_ = 0;
  uint16_t streamPacketRawLength_ = 0;
  uint8_t streamRaw_[RecordCompression::MAX_RAW] = {};
  Sha256 streamHash_;

  static SdFileBleService* instance_;
  static void receive(uint16_t connection, BLECharacteristic*, uint8_t* data, uint16_t length);
  void close();
  void nextEntry();
  void nextNewestEntry();
  void replyEntry(FsFile& entry, const char* name);
  void reply(uint8_t status, uint32_t value = 0, uint16_t length = 0);
  void error(const char* message);
  void pumpStream();
  uint16_t streamMtu();
};
