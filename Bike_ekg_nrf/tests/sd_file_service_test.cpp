#include "../SdFileBleService.h"
// Dienst über simulierte GATT-Schreibcallbacks antreiben; SD-Zugriffe zählen.
// Wiederholungen dürfen Listen nicht überspringen oder Metadaten-Token doppelt nutzen.
#include <assert.h>
#include <string.h>

bool SdService::begin() { ++testMounts; return !testMountError; }
void SdService::end() { ++testUnmounts; }
bool SdService::openRoot(FsFile& file) { return file.open("/", O_RDONLY); }
bool SdService::openRead(FsFile& file, const char* name) { return file.open(name, O_RDONLY); }
const char* SdService::lastError() const { return "SD card not detected"; }

using namespace SdFileProtocol;

int main() {
  assert(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926);
  assert(logName("EKG00001.BIN", 12));
  assert(!logName("../secrets", 10));
  assert(!logName("EKGabcde.BIN", 12));
  SdService sd;
  SdFileBleService service(sd);
  service.begin();
  auto* control = BLECharacteristic::registry.at("7f530002-1212-efde-1523-785feabcd123");
  auto* response = BLECharacteristic::registry.at("7f530003-1212-efde-1523-785feabcd123");
  uint16_t id = 0;
  auto send = [&](uint8_t op, std::vector<uint8_t> payload = {}, bool allowed = true) {
    std::vector<uint8_t> request = {0xF1, 1, 0, 0, op, 0};
    put16(request.data() + 2, ++id);
    request.insert(request.end(), payload.begin(), payload.end());
    control->request(request);
    service.update(allowed);
    assert(get16(response->value.data() + 2) == id);
    assert(response->value[4] == op);
    assert(get32(response->value.data() + 12) == crc32(response->value.data() + HEADER, response->value.size() - HEADER));
    return request;
  };
  testFiles["EKG00001.BIN"] = std::vector<uint8_t>(1048, 42);
  testFiles["EKG00002.BIN"] = std::vector<uint8_t>(1024, 7);
  testFiles["OTHER.TXT"] = std::vector<uint8_t>(10, 1);
  auto request = send(List);
  assert(service.active());
  assert(response->value[5] == Ok);
  assert(get32(response->value.data() + 8) == 1048);
  assert(!memcmp(response->value.data() + HEADER, "EKG00001.BIN", 12));
  const auto first = response->value;
  control->request(request); service.update(true);
  assert(response->value == first); // Retry cannot skip an entry.
  service.disconnected(); service.update(true);
  control->request(request); // A newly loaded page can reuse the same ID.
  assert(response->value[4] == 0); // Old response invalidated before write ACK.
  service.update(true);
  assert(response->value == first);
  send(Next);
  assert(!memcmp(response->value.data() + HEADER, "EKG00002.BIN", 12));
  send(Next);
  assert(response->value[5] == Done && !service.active());

  const std::string name = "EKG00001.BIN";
  const std::vector<uint8_t> path(name.begin(), name.end());
  send(Open, path);
  assert(service.active());
  std::vector<uint8_t> offset(4);
  send(Read, offset);
  assert(response->value.size() == 512);
  put32(offset.data(), 992);
  send(Read, offset);
  assert(response->value.size() == 16 + 56);
  put32(offset.data(), 1048);
  send(Read, offset);
  assert(response->value[5] == Done);
  send(Close);
  assert(!service.active());

  const int mounts = testMounts;
  const int unmounts = testUnmounts;
  send(List, {}, false);
  assert(response->value[5] == Error);
  assert(testMounts == mounts && testUnmounts == unmounts); // Never touch a recording's SD session.
  send(Open, std::vector<uint8_t>(12, '.'), true);
  assert(response->value[5] == Error && !service.active());
  testMountError = true;
  send(List);
  assert(response->value[5] == Error && !service.active());
  testMountError = false;
  send(Open, path);
  testReadError = true;
  send(Read, std::vector<uint8_t>(4));
  assert(response->value[5] == Error && !service.active());
  testReadError = false;

  send(Open, path);
  service.disconnected(); service.update(true);
  assert(!service.active());
  send(Open, path);
  testMillis += 15001;
  service.update(true);
  assert(!service.active() && response->value[5] == Error);
  testDirectoryError = true;
  testFiles.clear();
  send(List);
  assert(!service.active() && response->value[5] == Error);
  testDirectoryError = false;
  // Metadata staging is exclusive, checksum-protected, and single-use.
  std::vector<uint8_t> metadata(12 + 3 + 6);
  memcpy(metadata.data(), "META", 4);
  put16(metadata.data() + 4, 1); put16(metadata.data() + 6, 3); put16(metadata.data() + 8, 6);
  memcpy(metadata.data() + 12, "P01", 3); memcpy(metadata.data() + 15, "A\nB\tC!", 6);
  std::vector<uint8_t> metadataSize(2), checksum(4);
  put16(metadataSize.data(), metadata.size());
  put32(checksum.data(), crc32(metadata.data(), metadata.size()));
  auto stage = [&]() {
    send(MetadataBegin, metadataSize);
    const uint32_t token = get32(response->value.data() + 8);
    assert(token && service.active());
    for (size_t offset = 0; offset < metadata.size(); offset += 12) {
      const size_t count = std::min(size_t(12), metadata.size() - offset);
      std::vector<uint8_t> chunk(2 + count);
      put16(chunk.data(), offset);
      memcpy(chunk.data() + 2, metadata.data() + offset, count);
      const auto staged = send(MetadataChunk, chunk);
      assert(get32(response->value.data() + 8) == offset + count);
      control->request(staged); service.update(true); // Duplicate never appends twice.
    }
    return token;
  };
  const int mountsBeforeMetadata = testMounts;
  uint32_t token = stage();
  RecordingMetadata consumed;
  assert(!service.consumeMetadata(token, consumed)); // Not sealed yet.
  send(MetadataSeal, checksum);
  assert(!service.consumeMetadata(token + 1, consumed));
  assert(service.consumeMetadata(token, consumed));
  assert(consumed.length == metadata.size());
  assert(!memcmp(consumed.bytes, metadata.data(), metadata.size()));
  assert(!service.active() && !service.consumeMetadata(token, consumed));
  assert(testMounts == mountsBeforeMetadata); // Upload alone never powers SD.
  token = stage();
  send(MetadataSeal, std::vector<uint8_t>(4));
  assert(response->value[5] == Error && !service.consumeMetadata(token, consumed));
  token = stage(); send(MetadataSeal, checksum);
  service.disconnected(); service.update(true);
  assert(!service.consumeMetadata(token, consumed));
  token = stage(); send(MetadataSeal, checksum);
  testMillis += 15001; service.update(true);
  assert(!service.active() && !service.consumeMetadata(token, consumed));
  send(MetadataBegin, metadataSize, false);
  assert(response->value[5] == Error && !service.active());

  // Extended listing reads stored header text, plain legacy listing stays 12 bytes.
  testFiles.clear();
  auto& file = testFiles["EKG00001.BIN"];
  file.resize(1024);
  memcpy(file.data(), "BEKG", 4); put16(file.data() + 4, 1); put16(file.data() + 6, 512);
  put16(file.data() + 8, 12); put16(file.data() + 14, 4);
  memcpy(file.data() + 44, metadata.data(), metadata.size());
  testFiles["EKG00002.BIN"] = std::vector<uint8_t>(1024, 0);
  send(ListInfo);
  assert(response->value[HEADER + 12] == 1);
  assert(response->value.size() == HEADER + 13 + metadata.size());
  assert(!memcmp(response->value.data() + HEADER + 13, metadata.data(), metadata.size()));
  send(NextInfo);
  assert(response->value[HEADER + 12] == 2); // Broken header, still listed.
  send(NextInfo);
  assert(response->value[5] == Done && !service.active());
  const std::vector<uint8_t> savedHeader = file;
  testFiles.clear();
  for (int i = 0; i < 40; ++i) testFiles["AAA" + std::to_string(i)] = {};
  // Directory scanning yields after 16 irrelevant entries instead of blocking.
  std::vector<uint8_t> scan = {0xF1, 1, 0, 0, List, 0};
  put16(scan.data() + 2, ++id);
  control->request(scan);
  service.update(true);
  assert(response->value[4] == 0 && service.active());
  service.update(true);
  assert(response->value[4] == 0);
  service.update(true);
  assert(response->value[5] == Done && !service.active());

  // Newest-first listing scans names once, yielding without reading any headers.
  testFiles["EKG00000.BIN"] = savedHeader;
  testFiles["EKG00001.BIN"] = savedHeader;
  testFiles["EKG00500.BIN"] = std::vector<uint8_t>(1024, 0);
  testFiles["EKG99999.BIN"] = savedHeader;
  const int readsBefore = testReads;
  scan[4] = ListInfoNewest;
  put16(scan.data() + 2, ++id);
  control->request(scan); service.update(true);
  assert(response->value[4] == 0 && service.active());
  assert(testReads == readsBefore);
  control->request(scan); service.update(true); // Retry during indexing continues it, never restarts it.
  assert(response->value[4] == 0 && testReads == readsBefore);
  for (int i = 0; response->value[4] == 0 && i < 100; ++i) service.update(true);
  assert(response->value[4] == ListInfoNewest && response->value[5] == Ok);
  assert(!memcmp(response->value.data() + HEADER, "EKG99999.BIN", 12));
  assert(response->value[HEADER + 12] == 1 && testReads == readsBefore + 1);
  const auto newest = response->value;
  control->request(scan); service.update(true);
  assert(response->value == newest && testReads == readsBefore + 1);
  send(NextInfo);
  assert(!memcmp(response->value.data() + HEADER, "EKG00500.BIN", 12));
  assert(response->value[HEADER + 12] == 2);
  send(NextInfo);
  assert(!memcmp(response->value.data() + HEADER, "EKG00001.BIN", 12));
  send(NextInfo);
  assert(!memcmp(response->value.data() + HEADER, "EKG00000.BIN", 12));
  send(NextInfo);
  assert(response->value[5] == Done && !service.active());
  assert(testReads == readsBefore + 4);

  // Indexing can be cancelled or disconnected before the first entry arrives.
  put16(scan.data() + 2, ++id);
  control->request(scan); service.update(true);
  assert(response->value[4] == 0);
  send(Close);
  assert(!service.active());
  put16(scan.data() + 2, ++id);
  control->request(scan); service.update(true);
  service.disconnected(); service.update(true);
  assert(!service.active());

  // A new session clears all previous names, including the 00000/99999 edge bits.
  testFiles.clear();
  testFiles["EKG00002.BIN"] = savedHeader;
  send(ListInfoNewest);
  assert(!memcmp(response->value.data() + HEADER, "EKG00002.BIN", 12));
  send(NextInfo);
  assert(response->value[5] == Done && !service.active());
  testFiles.clear();
  send(ListInfoNewest);
  assert(response->value[5] == Done && !service.active());
  testDirectoryError = true;
  send(ListInfoNewest);
  assert(response->value[5] == Error && !service.active());
  testDirectoryError = false;
  return 0;
}
