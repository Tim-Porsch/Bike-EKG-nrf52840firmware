#include "../Sha256.h"
// Bekannte SHA-256-Vektoren einschließlich Million-'a' und wiederholter Finalisierung.
// Algorithmustest ist keine Sicherheitsfreigabe des gesamten BLE-Transports.
#include <assert.h>
#include <string>

static std::string hex(const Sha256& hash) {
  uint8_t digest[32];
  hash.digest(digest);
  const char* digits = "0123456789abcdef";
  std::string result;
  for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}

int main() {
  Sha256 hash;
  assert(hex(hash) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const uint8_t abc[] = {'a', 'b', 'c'};
  hash.update(abc, 1); hash.update(abc + 1, 2);
  assert(hex(hash) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  assert(hex(hash) == hex(hash)); // Finalization does not modify incremental state.
  hash.reset();
  const std::string vector = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  hash.update(reinterpret_cast<const uint8_t*>(vector.data()), vector.size());
  assert(hex(hash) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  hash.reset();
  const uint8_t a = 'a';
  for (int i = 0; i < 1000000; ++i) hash.update(&a, 1);
  assert(hex(hash) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
