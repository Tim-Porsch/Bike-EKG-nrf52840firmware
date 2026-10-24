#pragma once

// Zeilenorientierte USB-/NUS-Konsole mit getrennten Empfangspuffern.
// Die Quelle bleibt am Befehl, damit Antworten beim richtigen Client ankommen.

#include <Arduino.h>
#include <bluefruit.h>

enum class CommandSource : uint8_t {
  Serial,
  Ble,
};

struct ConsoleCommand {
  CommandSource source;
  char text[64];
};

class ConsoleService {
public:
  explicit ConsoleService(BLEUart& bleUart);

  bool poll(ConsoleCommand& command);
  void reply(CommandSource source, const char* text);
  void broadcast(const char* text);
  void setSerialRepliesEnabled(bool enabled);
  // Callback only marks the old session; queue mutation stays in loop().
  void disconnected();

private:
  static constexpr size_t LINE_BUFFER_SIZE = 64;
  static constexpr size_t BLE_CHUNK_SIZE = 20;
  static constexpr size_t BLE_TX_CAPACITY = 2048;
  static constexpr uint32_t BLE_TX_TIMEOUT_MS = 2000;
  static constexpr uint32_t BLE_TX_RETRY_MS = 20;

  BLEUart& bleUart_;
  char serialBuffer_[LINE_BUFFER_SIZE] = {};
  char bleBuffer_[LINE_BUFFER_SIZE] = {};
  size_t serialLength_ = 0;
  size_t bleLength_ = 0;
  bool serialOverflow_ = false;
  bool bleOverflow_ = false;
  bool serialRepliesEnabled_ = true;
  uint8_t bleTx_[BLE_TX_CAPACITY] = {};
  size_t bleTxHead_ = 0;
  size_t bleTxLength_ = 0;
  uint32_t bleTxProgressMs_ = 0;
  uint32_t bleTxAttemptMs_ = 0;
  bool bleTxRetry_ = false;
  bool bleTxNeedsSeparator_ = false;
  volatile bool bleResetPending_ = false;

  bool readLine(Stream& input, CommandSource source, char* buffer,
                size_t& length, bool& overflow, ConsoleCommand& command);
  void writeBle(const char* text);
  void updateBle();
  void resetBleSession();
};
