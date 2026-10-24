#include "ConsoleService.h"

#include <string.h>

ConsoleService::ConsoleService(BLEUart& bleUart) : bleUart_(bleUart) {}

bool ConsoleService::poll(ConsoleCommand& command) {
  updateBle();
  // USB zuerst prüfen; je Aufruf höchstens eine vollständige Zeile zurückgeben.
  return readLine(Serial, CommandSource::Serial, serialBuffer_, serialLength_,
                  serialOverflow_, command) ||
         readLine(bleUart_, CommandSource::Ble, bleBuffer_, bleLength_,
                  bleOverflow_, command);
}

void ConsoleService::reply(CommandSource source, const char* text) {
  if (source == CommandSource::Serial) {
    if (serialRepliesEnabled_) {
      Serial.print(text);
    }
    return;
  }

  writeBle(text);
}

void ConsoleService::broadcast(const char* text) {
  if (serialRepliesEnabled_) {
    Serial.print(text);
  }
  writeBle(text);
}

void ConsoleService::setSerialRepliesEnabled(bool enabled) {
  serialRepliesEnabled_ = enabled;
}

void ConsoleService::disconnected() {
  bleResetPending_ = true;
}

void ConsoleService::resetBleSession() {
  bleResetPending_ = false;
  bleTxHead_ = bleTxLength_ = 0;
  bleTxRetry_ = bleTxNeedsSeparator_ = false;
  bleLength_ = 0;
  bleOverflow_ = false;
}

void ConsoleService::writeBle(const char* text) {
  if (bleResetPending_) resetBleSession();
  if (!Bluefruit.connected() || !bleUart_.notifyEnabled()) return;

  size_t length = strlen(text);
  if (!length) return;
  // Copy stack-backed replies and preserve their order, including across a stall.
  if (length + (bleTxNeedsSeparator_ ? 2 : 0) > BLE_TX_CAPACITY - bleTxLength_) {
    bleTxHead_ = bleTxLength_ = 0;
    bleTxNeedsSeparator_ = false;
    text = "\r\nerror: console output overflow\r\n";
    length = strlen(text);
  }
  if (!bleTxLength_) {
    bleTxProgressMs_ = millis();
    bleTxRetry_ = false;
  }
  if (bleTxNeedsSeparator_) {
    bleTx_[(bleTxHead_ + bleTxLength_++) % BLE_TX_CAPACITY] = '\r';
    bleTx_[(bleTxHead_ + bleTxLength_++) % BLE_TX_CAPACITY] = '\n';
    bleTxNeedsSeparator_ = false;
  }
  for (size_t i = 0; i < length; ++i) {
    bleTx_[(bleTxHead_ + bleTxLength_++) % BLE_TX_CAPACITY] = text[i];
  }
}

void ConsoleService::updateBle() {
  if (bleResetPending_ || !Bluefruit.connected()) {
    resetBleSession();
    return;
  }
  if (!bleTxLength_) return;
  const uint32_t now = millis();
  if (!bleUart_.notifyEnabled() || now - bleTxProgressMs_ >= BLE_TX_TIMEOUT_MS) {
    bleTxHead_ = bleTxLength_ = 0;
    bleTxRetry_ = false;
    // Terminate a possibly partial line before the next response.
    bleTxNeedsSeparator_ = true;
    return;
  }
  if (bleTxRetry_ && now - bleTxAttemptMs_ < BLE_TX_RETRY_MS) return;

  uint8_t chunk[BLE_CHUNK_SIZE];
  const size_t length = bleTxLength_ < BLE_CHUNK_SIZE ? bleTxLength_ : BLE_CHUNK_SIZE;
  for (size_t i = 0; i < length; ++i) {
    chunk[i] = bleTx_[(bleTxHead_ + i) % BLE_TX_CAPACITY];
  }
  // At most one BLE write per loop pass; never spin on a full notification queue.
  // BLEUart writes a single <=20-byte notification atomically (all or nothing).
  const size_t written = bleUart_.write(chunk, length);
  if (bleResetPending_) {
    resetBleSession();
    return;
  }
  bleTxAttemptMs_ = millis();
  bleTxRetry_ = written != length;
  if (!bleTxRetry_) {
    bleTxHead_ = (bleTxHead_ + length) % BLE_TX_CAPACITY;
    bleTxLength_ -= length;
    bleTxProgressMs_ = millis();
  }
}

bool ConsoleService::readLine(Stream& input, CommandSource source,
                              char* buffer, size_t& length, bool& overflow,
                              ConsoleCommand& command) {
  while (input.available()) {
    const int value = input.read();
    if (value < 0 || value == '\r') {
      continue;
    }

    if (value == '\n') {
      if (overflow) {
        overflow = false;
        length = 0;
        command.source = source;
        strcpy(command.text, "!line-too-long");
        return true;
      }

      if (length == 0) {
        continue;
      }

      buffer[length] = '\0';
      command.source = source;
      strcpy(command.text, buffer);
      length = 0;
      return true;
    }

    if (length + 1 >= LINE_BUFFER_SIZE) {
      overflow = true;
      continue;
    }

    if (!overflow) {
      buffer[length++] = static_cast<char>(value);
    }
  }

  return false;
}
