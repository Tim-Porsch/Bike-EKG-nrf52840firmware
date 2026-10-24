#include "../PreviewBuffer.h"
// Hardwarefreier Test des Paketlayouts, Queue-Rückstaus und Nummernüberlaufs.
// Assertions aktiv lassen (nicht mit -DNDEBUG bauen).
#include <assert.h>
#include <stdint.h>

static uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

int main() {
  PreviewBuffer queue;
  queue.reset(1234);
  queue.append(0, -8388608, 2);
  assert(!queue.front());
  queue.append(1, 8388607, 2);
  const auto* frame = queue.front();
  assert(frame && frame->length == 20);
  assert(frame->bytes[0] == 0xD1 && frame->bytes[1] == 1);
  assert((frame->bytes[2] | frame->bytes[3] << 8) == 1234);
  assert(frame->bytes[10] == 2 && u32(frame->bytes + 6) == 0);
  assert(u32(frame->bytes + 12) == uint32_t(-8388608));
  assert(u32(frame->bytes + 16) == uint32_t(8388607));
  queue.pop();
  queue.append(2, 5, 58);
  queue.append(7, 10, 58); // Source gap must split a partial frame.
  assert(queue.front()->bytes[10] == 1);
  assert(u32(queue.front()->bytes + 6) == 2);
  queue.pop();
  queue.flush();
  assert(u32(queue.front()->bytes + 6) == 7);
  queue.pop();

  queue.reset(2);
  for (uint32_t i = 0; i < 20; ++i) queue.append(i, i, 2);
  assert(queue.dropped() == 4); // Ten frames, only the newest eight survive.
  assert(u32(queue.front()->bytes + 6) == 4);
  queue.append(20, 0, 58);
  queue.discard();
  assert(!queue.front() && queue.dropped() == 21);
  queue.reset(3);
  assert(queue.dropped() == 0);
  // Natural rollover remains contiguous.
  queue.append(UINT32_MAX, -1, 58);
  queue.append(0, 2, 58);
  queue.flush();
  assert(queue.front()->bytes[10] == 2);
  queue.pop();
  for (uint32_t i = 0; i < 58; ++i) queue.append(i, i, 58);
  assert(queue.front()->length == 244);
  queue.pop();
  // Shrinking MTU flushes the old partial block before appending a new sample.
  queue.append(100, 1, 58);
  queue.append(101, 2, 58);
  queue.append(102, 3, 2);
  assert(queue.front()->bytes[10] == 2);
  queue.reset(4);
  for (uint32_t i = 0; i < 65538; ++i) {
    queue.append(i, -5, 1);
    const auto* current = queue.front();
    assert(current);
    assert(uint16_t(current->bytes[4] | uint16_t(current->bytes[5]) << 8) == uint16_t(i));
    queue.pop();
  }
  return 0;
}
