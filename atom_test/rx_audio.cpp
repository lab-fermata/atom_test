// rx_audio.cpp — RX（A2DP Sink）
// 段階1: Bluetooth なしの骨組み。接続状態は常に未接続
#include <Arduino.h>
#include "rx_audio.h"
#include "dial_link.h"
#include "status_led.h"

static bool s_connected = false;

void rxSetup() {
  ledSet(LED_BLUE_BLINK);
}

void rxLoop() {
}

void rxSendStatus() {
  if (s_connected) {
    // 段階2で実装
  } else {
    dialSendDisconnected();
  }
}

void rxKill() {
  if (!s_connected) {
    LOG1("KILL: not connected");
    return;
  }
}
