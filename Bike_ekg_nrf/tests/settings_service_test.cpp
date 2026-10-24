#include "../SettingsService.h"
#include <InternalFileSystem.h>
#include <assert.h>
#include <iostream>

SettingsFileSystemStub InternalFS;
static const char* path = "/bike_ekg_settings.bin";
static const char* temporary = "/bike_ekg_settings.tmp";

struct __attribute__((packed)) V1 {
  uint32_t magic = 0x53474B42;
  uint8_t version = 1;
  uint16_t rate = 250;
  uint8_t gain = 8;
  uint8_t input = static_cast<uint8_t>(Ads1291::InputMux::InputShorted);
  uint32_t duration = 60;
  uint32_t checksum = 0;
};
struct __attribute__((packed)) V2 {
  uint32_t magic = 0x53474B42;
  uint8_t version = 2;
  uint16_t rate = 125;
  uint8_t gain = 12;
  uint8_t input = static_cast<uint8_t>(Ads1291::InputMux::TestSignal);
  uint32_t duration = 42;
  uint16_t low = 1000;
  uint16_t high = 50000;
  uint32_t checksum = 0;
};
template <typename T> static void sign(T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(T) - 4; ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  value.checksum = hash;
}
template <typename T> static void store(T value) {
  sign(value);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  InternalFS.files[path].assign(bytes, bytes + sizeof(T));
}
static void defaults(const SettingsService& service) {
  assert(service.sampleRateSps() == 500);
  assert(service.gainMultiplier() == 6);
  assert(std::string(service.inputName()) == "normal");
  assert(service.current().testDurationSeconds == 300);
  assert(service.current().rldEnabled);
  assert(service.filterLowCutoffMilliHz() == 500);
  assert(service.filterHighCutoffMilliHz() == 40000);
}
int main() {
  {
    SettingsService service;
    assert(service.begin());
    defaults(service);
    assert(service.reset());
    const auto saved = InternalFS.files.at(path);
    assert(saved.size() == 22 && saved[4] == 3 && saved[17] == 1);
    SettingsService reboot;
    assert(reboot.begin());
    defaults(reboot);
    reboot.setRldEnabled(false);
    assert(reboot.setSampleRate(250));
    assert(reboot.setGain(8));
    assert(reboot.setInput("short"));
    assert(reboot.setTestDurationSeconds(90));
    assert(reboot.setFilterBandMilliHz(1000, 50000));
    assert(reboot.save());
    SettingsService off;
    assert(off.begin());
    assert(!off.current().rldEnabled);
    assert(off.sampleRateSps() == 250 && off.gainMultiplier() == 8);
    assert(std::string(off.inputName()) == "short");
    assert(off.current().testDurationSeconds == 90);
    assert(off.filterLowCutoffMilliHz() == 1000 && off.filterHighCutoffMilliHz() == 50000);
    off.setRldEnabled(true);
    assert(off.save());
    SettingsService on;
    assert(on.begin());
    assert(on.current().rldEnabled);
    assert(on.reset());
    defaults(on);
  }
  {
    store(V1{});
    SettingsService legacy;
    assert(legacy.begin());
    assert(legacy.sampleRateSps() == 250 && legacy.gainMultiplier() == 8);
    assert(std::string(legacy.inputName()) == "short");
    assert(legacy.current().testDurationSeconds == 60);
    assert(!legacy.current().rldEnabled); // historical implicit state, not new defaults
    assert(legacy.filterLowCutoffMilliHz() == 500 && legacy.filterHighCutoffMilliHz() == 40000);
    assert(legacy.save());
    SettingsService reboot;
    assert(reboot.begin() && !reboot.current().rldEnabled);
    assert(InternalFS.files.at(path)[4] == 3);
    store(V2{});
    assert(legacy.begin());
    assert(legacy.sampleRateSps() == 125 && legacy.gainMultiplier() == 12);
    assert(std::string(legacy.inputName()) == "test");
    assert(legacy.current().testDurationSeconds == 42);
    assert(legacy.filterLowCutoffMilliHz() == 1000 && legacy.filterHighCutoffMilliHz() == 50000);
    assert(!legacy.current().rldEnabled);
    assert(legacy.save());
    assert(reboot.begin());
    assert(!reboot.current().rldEnabled && reboot.sampleRateSps() == 125);
  }
  {
    SettingsService service;
    assert(service.begin());
    assert(service.reset());
    const auto valid = InternalFS.files.at(path);
    // Checksum corruption, truncation and trailing data are rejected atomically.
    for (int mode = 0; mode < 3; ++mode) {
      auto bytes = valid;
      if (mode == 0) bytes[17] ^= 1;
      if (mode == 1) bytes.pop_back();
      if (mode == 2) bytes.push_back(0);
      InternalFS.files[path] = bytes;
      assert(service.begin());
      defaults(service);
    }
    // Even a correctly checksummed V3 boolean outside 0/1 is invalid.
    auto invalidBoolean = valid;
    invalidBoolean[17] = 2;
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < invalidBoolean.size() - 4; ++i) hash = (hash ^ invalidBoolean[i]) * 16777619UL;
    for (size_t i = 0; i < 4; ++i) invalidBoolean[18 + i] = static_cast<uint8_t>(hash >> (8 * i));
    InternalFS.files[path] = invalidBoolean;
    assert(service.begin());
    defaults(service);
    for (int mode = 0; mode < 4; ++mode) {
      V2 invalid;
      if (mode == 0) invalid.rate = 100;
      if (mode == 1) invalid.gain = 7;
      if (mode == 2) invalid.input = 255;
      if (mode == 3) invalid.high = 63000;
      store(invalid);
      assert(service.begin());
      defaults(service);
    }
    assert(service.save());
    const auto old = InternalFS.files.at(path);
    service.setRldEnabled(false);
    assert(service.setGain(8));
    for (int mode = 0; mode < 3; ++mode) {
      InternalFS.openWriteOk = mode != 0;
      InternalFS.writeLimit = mode == 1 ? 5 : SIZE_MAX;
      InternalFS.renameOk = mode != 2;
      assert(!service.reset());
      assert(!service.current().rldEnabled && service.gainMultiplier() == 8);
      assert(InternalFS.files.at(path) == old);
      assert(!InternalFS.files.count(temporary));
    }
    InternalFS = SettingsFileSystemStub{};
    InternalFS.mountOk = false;
    assert(!service.begin());
    defaults(service);
  }
  std::cout << "settings_service_test: OK\n";
}
