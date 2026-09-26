// status_led.h — 本体RGB LED（G27、TX/RX 共通）
#pragma once

enum LedMode {
  LED_OFF,
  LED_BLUE_BLINK,  // BT接続待ち
  LED_BLUE_ON,     // BT接続中
};

void ledBegin();  // M5.begin() の後に呼ぶ
void ledSet(LedMode mode);
void ledUpdate();  // loop() から呼ぶ（点滅用）
