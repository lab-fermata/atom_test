// status_led.cpp — 本体RGB LED（G27、TX/RX 共通）
//
// M5Unified の M5.Led を使う。ATOM Lite では M5.begin() 内の _setup_led() が
// G27 に LED_Strip_Class（RMT駆動、GRB順）を割り当てる（M5Unified.inl の _setup_led と
// _pin_table_other0 の board_M5AtomLite 行で確認）。
#include <M5Unified.h>
#include "status_led.h"
#include "config.h"

static LedMode s_mode = LED_OFF;
static bool s_lit = false;
static uint32_t s_lastToggle = 0;

static void show(bool lit) {
  s_lit = lit;
  if (lit) {
    M5.Led.setAllColor(0, 0, LED_LEVEL);
  } else {
    M5.Led.setAllColor(0, 0, 0);
  }
}

void ledBegin() {
  M5.Led.setBrightness(255);  // 輝度は色の値（LED_LEVEL）で決める
  show(false);
}

void ledSet(LedMode mode) {
  if (mode == s_mode) return;
  s_mode = mode;
  s_lastToggle = millis();
  show(mode != LED_OFF);
}

void ledUpdate() {
  if (s_mode != LED_BLUE_BLINK) return;
  uint32_t now = millis();
  if (now - s_lastToggle >= LED_BLINK_MS) {
    s_lastToggle = now;
    show(!s_lit);
  }
}
