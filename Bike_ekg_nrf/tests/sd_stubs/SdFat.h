#pragma once
// In-Memory-Dateisystem nur für die verwendeten API-Teile, mit injizierbaren Fehlern.
// Kein FAT-/Powerloss-/Timingmodell; Produktionscode darf diesen Include-Pfad nicht nutzen.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

#define O_RDONLY 0
#define O_RDWR 1
#define O_CREAT 2
#define O_EXCL 4
#define SHARED_SPI 0
#define SD_SCK_MHZ(mhz) ((mhz) * 1000000UL)
using SdCsPin_t = uint8_t;
struct SdSpiConfig { SdSpiConfig(int, int, uint32_t) {} };
inline std::map<std::string, std::vector<uint8_t>> testFiles;
inline bool testReadError = false;
inline bool testMountError = false;
inline bool testDirectoryError = false;
inline int testMounts = 0;
inline int testUnmounts = 0;
inline int testReads = 0;
class SdFat {
public:
  bool begin(SdSpiConfig) { ++testMounts; return !testMountError; }
  void end() { ++testUnmounts; }
  bool exists(const char* name) const { return testFiles.count(name); }
};
class FsFile {
public:
  bool open(const char* name, int) {
    name_ = name;
    cursor_ = 0;
    open_ = name_ == "/" || testFiles.count(name_);
    return open_;
  }
  bool openNext(FsFile* directory, int) {
    auto entry = testFiles.begin();
    if (directory->cursor_ >= testFiles.size()) return false;
    std::advance(entry, directory->cursor_++);
    return open(entry->first.c_str(), O_RDONLY);
  }
  void getName(char* name, size_t size) const {
    strncpy(name, name_.c_str(), size - 1); name[size - 1] = 0;
  }
  bool isFile() const { return open_ && name_ != "/"; }
  bool isOpen() const { return open_; }
  uint64_t fileSize() const { return testFiles.at(name_).size(); }
  bool close() { open_ = false; return true; }
  explicit operator bool() const { return open_; }
  bool preAllocate(uint64_t bytes) { testFiles.at(name_).resize(bytes); return true; }
  bool truncate() { testFiles.at(name_).resize(cursor_); return true; }
  bool sync() const { return true; }
  size_t write(const void* data, size_t count) {
    auto& destination = testFiles.at(name_);
    if (destination.size() < cursor_ + count) destination.resize(cursor_ + count);
    memcpy(destination.data() + cursor_, data, count);
    cursor_ += count;
    return count;
  }
  uint8_t getError() const { return testDirectoryError ? 1 : 0; }
  bool seekSet(uint32_t offset) { cursor_ = offset; return offset <= fileSize(); }
  int read(uint8_t* bytes, size_t count) {
    ++testReads;
    if (testReadError) return -1;
    const auto& source = testFiles.at(name_);
    count = std::min(count, source.size() - cursor_);
    memcpy(bytes, source.data() + cursor_, count);
    cursor_ += count;
    return count;
  }
private:
  bool open_ = false;
  std::string name_;
  size_t cursor_ = 0;
};
