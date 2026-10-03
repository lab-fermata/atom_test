// status_led.h — 本体RGB LED（G27、TX/RX 共通）
// コピー元: BT_SPEAKER/atom_a2dp/status_led.h（c1d9300）。LED_RED_BLINK を足した
#pragma once

enum LedMode {
  LED_OFF,
  LED_BLUE_BLINK,  // BT接続待ち
  LED_BLUE_ON,     // BT接続中
  LED_RED,         // TX: 接続中かつエラー検出中（atom_a2dp では TX 未実装の表示にも使う）
  LED_RED_BLINK,   // TX: 接続待ちかつエラー検出中
};

void ledBegin();  // M5.begin() の後に呼ぶ
void ledSet(LedMode mode);
void ledUpdate();  // loop() から呼ぶ（点滅用）
