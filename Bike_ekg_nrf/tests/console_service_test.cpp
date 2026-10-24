// Host regression: a full three-packet notification queue must not lose a reply.
#include "../ConsoleService.h"
#include <assert.h>
#include <string.h>
#include <iostream>

uint32_t testMillis = 0;
SerialStub Serial;
BluefruitStub Bluefruit;

static void pump(ConsoleService& console, size_t passes = 200) {
  ConsoleCommand command;
  for (size_t i = 0; i < passes; ++i) {
    console.poll(command);
    ++testMillis;
  }
}

int main() {
  const std::string config =
      "config:\r\n  firmware: 0.12.2\r\n  rate: 250 SPS\r\n  gain: x6\r\n"
      "  input: normal\r\n  rld: off\r\n  test_duration: 60 seconds\r\n"
      "  filter_band: 0.500-40.000 Hz\r\n  stream_filter: off\r\n"
      "  preview_max_rate: 500 SPS\r\n  apply: at next start\r\n";
  {
    BLEUart uart;
    ConsoleService console(uart);
    char message[320];
    strcpy(message, config.c_str());
    console.reply(CommandSource::Ble, message);
    memset(message, '!', sizeof(message)); // reply must own the queued bytes
    pump(console, 3);
    assert(uart.output == config.substr(0, 60));
    uart.stalled = true;
    uart.blockedMs = 100; // match the underlying Bluefruit semaphore timeout
    pump(console, 1);
    const size_t attempts = uart.attempts;
    pump(console, 10);
    assert(uart.attempts == attempts); // no busy-loop retry
    console.reply(CommandSource::Ble, "state: idle\r\n");
    testMillis += 100;
    uart.stalled = false;
    pump(console);
    assert(uart.output == config + "state: idle\r\n");
    // Ring wrap and consecutive replies must preserve all bytes.
    for (size_t i = 0; i < 30; ++i) {
      console.reply(CommandSource::Ble, config.c_str());
      pump(console);
    }
    std::string expected = config + "state: idle\r\n";
    for (size_t i = 0; i < 30; ++i) expected += config;
    assert(uart.output == expected);
  }
  {
    BLEUart uart;
    ConsoleService console(uart);
    console.reply(CommandSource::Ble, config.c_str());
    pump(console, 3);
    uart.stalled = true;
    pump(console, 1);
    testMillis += 2000;
    pump(console, 1); // timeout discards old bytes, not indefinitely retried
    const size_t attempts = uart.attempts;
    pump(console);
    assert(uart.attempts == attempts);
    uart.stalled = false;
    console.reply(CommandSource::Ble, config.c_str());
    pump(console);
    assert(uart.output == config.substr(0, 60) + "\r\n" + config);
  }
  {
    BLEUart uart;
    ConsoleService console(uart);
    console.reply(CommandSource::Ble, config.c_str());
    pump(console, 3);
    // Disconnect/reconnect even between loop passes must drop the old tail.
    console.disconnected();
    console.reply(CommandSource::Ble, "state: idle\r\n");
    pump(console);
    assert(uart.output == config.substr(0, 60) + "state: idle\r\n");
    console.reply(CommandSource::Ble, config.c_str());
    Bluefruit.active = false;
    pump(console);
    Bluefruit.active = true;
    const std::string before = uart.output;
    pump(console);
    assert(uart.output == before);
    console.reply(CommandSource::Ble, config.c_str());
    uart.subscribed = false;
    pump(console);
    uart.subscribed = true;
    console.reply(CommandSource::Ble, "state: idle\r\n");
    pump(console);
    assert(uart.output == before + "\r\nstate: idle\r\n");
  }
  {
    BLEUart uart;
    ConsoleService console(uart);
    const std::string tooLarge(3000, 'x');
    console.reply(CommandSource::Ble, config.c_str());
    console.reply(CommandSource::Ble, tooLarge.c_str());
    pump(console);
    assert(uart.output == "\r\nerror: console output overflow\r\n");
    console.reply(CommandSource::Serial, "USB unaffected\r\n");
    assert(Serial.output == "USB unaffected\r\n");
  }
  {
    testMillis = UINT32_MAX - 50;
    BLEUart uart;
    ConsoleService console(uart);
    console.reply(CommandSource::Ble, config.c_str());
    pump(console, 3);
    uart.stalled = true;
    pump(console, 1);
    testMillis += 150; // millis() wrap must not expire a healthy pending reply
    uart.stalled = false;
    pump(console);
    assert(uart.output == config);
  }
  std::cout << "console_service_test: OK\n";
}
