#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define FILE_O_READ 1
#define FILE_O_WRITE 2

struct SettingsFileSystemStub {
  std::map<std::string, std::vector<uint8_t>> files;
  bool mountOk = true;
  bool openWriteOk = true;
  bool renameOk = true;
  size_t writeLimit = SIZE_MAX;
  bool begin() { return mountOk; }
  void remove(const char* name) { files.erase(name); }
  bool rename(const char* from, const char* to) {
    if (!renameOk) return false;
    files[to] = files.at(from);
    files.erase(from);
    return true;
  }
};

namespace Adafruit_LittleFS_Namespace {
class File {
public:
  explicit File(SettingsFileSystemStub& fs) : fs_(fs) {}
  bool open(const char* name, int mode) {
    name_ = name;
    position_ = 0;
    if (mode == FILE_O_WRITE) {
      if (!fs_.openWriteOk) return false;
      fs_.files[name_].clear();
      return true;
    }
    return fs_.files.count(name_) != 0;
  }
  int read(void* destination, size_t length) {
    const auto& bytes = fs_.files.at(name_);
    const size_t count = std::min(length, bytes.size() - position_);
    if (count) std::memcpy(destination, bytes.data() + position_, count);
    position_ += count;
    return static_cast<int>(count);
  }
  int available() { return static_cast<int>(fs_.files.at(name_).size() - position_); }
  size_t write(const uint8_t* data, size_t length) {
    const size_t count = std::min(length, fs_.writeLimit);
    fs_.files[name_].assign(data, data + count);
    return count;
  }
  void close() {}
private:
  SettingsFileSystemStub& fs_;
  std::string name_;
  size_t position_ = 0;
};
}
