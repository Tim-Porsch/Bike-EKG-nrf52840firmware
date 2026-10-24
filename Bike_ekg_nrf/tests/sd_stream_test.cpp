#include "../SdFileBleService.h"
// Roh- und komprimierten Stream bei MTU 23/247 bytegenau rekonstruieren.
// Rückstau, Fenster-ACKs, Seek-Wiederholungen und stabilen Abschluss-Hash prüfen.
#include <assert.h>
#include <string.h>

bool SdService::begin() { return true; }
void SdService::end() {}
bool SdService::openRoot(FsFile& file) { return file.open("/", O_RDONLY); }
bool SdService::openRead(FsFile& file, const char* name) { return file.open(name, O_RDONLY); }
const char* SdService::lastError() const { return "SD error"; }

using namespace SdFileProtocol;

static std::vector<uint8_t> unpack(const std::vector<uint8_t>& packet) {
  if (packet[1] == 1) return std::vector<uint8_t>(packet.begin() + 12, packet.end());
  assert(packet[1] == 2 && packet.size() >= 26);
  std::vector<uint8_t> raw(get16(packet.data() + 12));
  assert(raw.size() <= 384 && raw.size() % 12 == 0 && raw.size() > packet.size() - 12);
  memcpy(raw.data(), packet.data() + 14, 12);
  size_t input = 26;
  for (size_t offset = 12; offset < raw.size(); offset += 4) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < 5; ++i) {
      assert(input < packet.size());
      const uint8_t byte = packet[input++];
      assert(i < 4 || byte <= 15);
      value |= uint32_t(byte & 127) << (i * 7);
      if (!(byte & 128)) break;
      assert(i < 4);
    }
    const uint32_t delta = (value >> 1) ^ (0u - (value & 1));
    put32(raw.data() + offset, get32(raw.data() + offset - 12) + delta);
  }
  assert(input == packet.size());
  return raw;
}

int main() {
  SdService sd;
  SdFileBleService service(sd);
  service.begin();
  auto* control = BLECharacteristic::registry.at("7f530002-1212-efde-1523-785feabcd123");
  auto* response = BLECharacteristic::registry.at("7f530003-1212-efde-1523-785feabcd123");
  auto* data = BLECharacteristic::registry.at("7f530004-1212-efde-1523-785feabcd123");
  uint16_t id = 0;
  auto send = [&](uint8_t op, std::vector<uint8_t> payload = {}) {
    std::vector<uint8_t> request = {0xF1, 1, 0, 0, op, 0};
    put16(request.data() + 2, ++id);
    request.insert(request.end(), payload.begin(), payload.end());
    control->request(request); service.update(true);
    assert(response->value[4] == op);
    assert(get16(response->value.data() + 2) == id);
    return request;
  };
  const char* name = "EKG00001.BIN";
  auto open = [&]() { send(Open, std::vector<uint8_t>(name, name + 12)); };
  for (const int scenario : {0, 1, 2}) {
  std::vector<uint8_t> source(scenario ? 1024 + 12 * 1000 : 12345);
  for (size_t i = 0; i < source.size(); ++i) source[i] = i * 13;
  uint32_t random = 123456789;
  if (scenario) for (uint32_t i = 0; i < 1000; ++i) {
    auto nextRandom = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    put32(source.data() + 512 + i * 12, scenario == 2 ? nextRandom() : i + (i > 20 ? 7 : 0));
    put32(source.data() + 516 + i * 12, scenario == 2 ? nextRandom() : 0xFFFFF000u + i * 2000);
    put32(source.data() + 520 + i * 12, scenario == 2 ? nextRandom() : (i % 9 ? uint32_t(-1000) + i : 0x80000000u));
  }
  testFiles["EKG00001.BIN"] = source;
  for (const uint16_t mtu : {23, 247}) {
   for (const uint8_t operation : {uint8_t(StreamStart), uint8_t(StreamStartCompressed)}) {
    Bluefruit.connection.mtu = mtu;
    data->notifications.clear();
    open();
    send(operation);
    const uint16_t session = get32(response->value.data() + 8);
    assert(session && get32(response->value.data() + HEADER) == source.size());
    assert(get16(response->value.data() + HEADER + 4) == 4096);
    auto position = [&](uint32_t offset) {
      std::vector<uint8_t> payload(6);
      put16(payload.data(), session); put32(payload.data() + 2, offset);
      return payload;
    };
    for (int i = 0; i < 600; ++i) service.update(true);
    uint32_t delivered = 0;
    for (const auto& packet : data->notifications) delivered += unpack(packet).size();
    assert(delivered == 4096); // Never overrun receive window without ACK.
    const size_t before = data->notifications.size();
    service.update(true);
    assert(data->notifications.size() == before);
    // Rewind across previously hashed bytes; change packet boundaries at 1001.
    send(StreamSeek, position(1001));
    const auto duplicate = send(StreamAck, position(1001));
    control->request(duplicate); service.update(true);
    data->notifications.clear();
    // Force a pending SD packet and retry it without advancing file position.
    data->blocked = true;
    service.update(true);
    assert(data->notifications.empty());
    data->blocked = false;
    send(StreamSeek, position(1001));
    uint32_t received = 1001;
    std::vector<uint8_t> result(source.size());
    memcpy(result.data(), source.data(), received);
    size_t processed = 0;
    size_t compressedPackets = 0;
    while (received < source.size()) {
      service.update(true);
      while (processed < data->notifications.size()) {
        const auto& packet = data->notifications[processed++];
        assert(packet.size() <= size_t(mtu - 3));
        assert(packet[0] == 0xF3 && (packet[1] == 1 || packet[1] == 2));
        if (packet[1] == 2) ++compressedPackets;
        assert(get16(packet.data() + 2) == session);
        assert(get32(packet.data() + 4) == received);
        const uint32_t crc = ~crc32Update(crc32Update(0xFFFFFFFF, packet.data(), 8), packet.data() + 12, packet.size() - 12);
        assert(get32(packet.data() + 8) == crc);
        const auto raw = unpack(packet);
        memcpy(result.data() + received, raw.data(), raw.size());
        received += raw.size();
      }
      send(StreamAck, position(received));
    }
    assert(result == source);
    if (scenario == 1 && mtu == 247 && operation == StreamStartCompressed) assert(compressedPackets > 0);
    if (mtu == 23 || operation == StreamStart || scenario != 1) assert(compressedPackets == 0);
    const auto token = position(received);
    send(StreamFinish, std::vector<uint8_t>(token.begin(), token.begin() + 2));
    assert(response->value[5] == Ok && get16(response->value.data() + 6) == 32);
    uint8_t digest[32]; Sha256 hash; hash.update(source.data(), source.size()); hash.digest(digest);
    assert(!memcmp(response->value.data() + HEADER, digest, 32));
    send(Close);
    assert(!service.active());
   }
  }
  }
  open(); send(StreamStart);
  send(StreamFinish, {0, 0});
  assert(response->value[5] == Error && !service.active());
  open(); send(StreamStart);
  data->subscribed = false;
  service.update(true);
  assert(!service.active());
  data->subscribed = true;
  open(); send(StreamStart);
  service.disconnected(); service.update(true);
  assert(!service.active());
}
