#pragma once
// Synchrone GATT-Doubles speichern Werte/Notifications und simulieren TX-Rückstau.
// Kritische Bereiche sind No-ops; echte SoftDevice-/Task-Rennen bleiben Hardwaretests.
#include <stdint.h>
#include <stddef.h>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

#define CHR_PROPS_WRITE 1
#define CHR_PROPS_READ 2
#define CHR_PROPS_NOTIFY 4
#define SECMODE_OPEN 1
#define SECMODE_NO_ACCESS 0
#define taskENTER_CRITICAL() do {} while (0)
#define taskEXIT_CRITICAL() do {} while (0)
using std::min;
inline uint32_t testMillis = 0;
inline uint32_t millis() { return testMillis; }
struct BLEConnection { uint16_t mtu = 247; uint16_t getMtu() const { return mtu; } };
struct TestBluefruit {
  struct Peripheral { bool connected() const { return connected_; } bool connected_ = true; } Periph;
  uint16_t connHandle() const { return 1; }
  BLEConnection connection;
  BLEConnection* Connection(uint16_t) { return &connection; }
};
inline TestBluefruit Bluefruit;
class BLEService {
public:
  explicit BLEService(const char*) {}
  void begin() {}
};
class BLECharacteristic {
public:
  using Callback = void (*)(uint16_t, BLECharacteristic*, uint8_t*, uint16_t);
  inline static std::map<std::string, BLECharacteristic*> registry;
  explicit BLECharacteristic(const char* uuid) : uuid_(uuid) {}
  void setProperties(int) {}
  void setPermission(int, int) {}
  void setMaxLen(int) {}
  void setWriteCallback(Callback callback, bool) { callback_ = callback; }
  void begin() { registry[uuid_] = this; }
  void write(const uint8_t* bytes, size_t length) { value.assign(bytes, bytes + length); }
  void request(std::vector<uint8_t> bytes) { callback_(1, this, bytes.data(), bytes.size()); }
  bool notifyEnabled() const { return subscribed; }
  bool notify(const uint8_t* bytes, size_t length) {
    if (!subscribed || blocked) return false;
    notifications.emplace_back(bytes, bytes + length);
    return true;
  }
  bool subscribed = true, blocked = false;
  std::vector<std::vector<uint8_t>> notifications;
  std::vector<uint8_t> value;
private:
  std::string uuid_;
  Callback callback_ = nullptr;
};
