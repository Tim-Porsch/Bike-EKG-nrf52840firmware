#pragma once
#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <string>

extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }

class Stream {
public:
  std::deque<uint8_t> input;
  int available() { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    const uint8_t byte = input.front();
    input.pop_front();
    return byte;
  }
};

class SerialStub : public Stream {
public:
  std::string output;
  void print(const char* text) { output += text; }
};
extern SerialStub Serial;
