// tx_main.cpp — TX の役割の入口: 状態の管理、LED、ボタン、切断時の再起動（atom_tx_test_instructions.md 4.1節）、
// S/PDIF が無いときは BT 接続しない（TX_BT_NEED_SPDIF）
// 形は BT_SPEAKER/atom_a2dp/rx_main.cpp（c0b5439）を参考にした
#include <M5Unified.h>
#include <esp_a2dp_api.h>
#include <esp_attr.h>
#include <esp_system.h>
#include "tx_main.h"
#include "config.h"
#include "dial_link.h"
#include "status_led.h"
#include "tx_audio.h"

static bool s_connected = false;  // loop() 側で確定した接続状態
static bool s_simError = false;   // 模擬エラー（S/PDIF のエラーの代わり）
static bool s_error = false;      // エラー中（S/PDIF のエラー または 模擬エラー）
// 起動時にボタンが押されていた（長押しの再起動から押し続けている）。最初に離したときは短押しとして扱わない
static bool s_ignoreRelease = false;

// 模擬エラーは、ソフトウェアの再起動（ESP.restart()）をまたいで覚えておく（S/PDIF が途絶えたまま再起動した状態を
// 模擬するため）。電源投入・長押しの再起動では消す
static constexpr uint32_t kRtcMagic = 0x53504446;  // "SPDF"
RTC_NOINIT_ATTR static uint32_t s_rtcMagic;
RTC_NOINIT_ATTR static uint32_t s_rtcSimError;

static void setSimError(bool on) {
  s_simError = on;
  s_rtcSimError = on;
  s_rtcMagic = kRtcMagic;
}

// 色（赤・青）で S/PDIF のエラーの有無、点滅・点灯で BT の接続状態を表す（software.md 2.4節）
static void updateLed() {
  if (s_connected) {
    ledSet(s_error ? LED_RED : LED_BLUE_ON);
  } else {
    ledSet(s_error ? LED_RED_BLINK : LED_BLUE_BLINK);
  }
}

// S/PDIF のエラーと模擬エラーをまとめ、変わったらログを出して tx_audio に渡す（無音・AVRCP の通知）
static void updateError() {
  bool spdif = txAudioSpdifError();
  bool err = spdif || s_simError;
  static bool lastSpdif = false;
  if (spdif != lastSpdif) {
    lastSpdif = spdif;
    LOG1("spdif error: %s", spdif ? txAudioSpdifReason() : "cleared");
  }
  if (err != s_error) {
    s_error = err;
    txAudioSetError(err);
  }
}

static void restart(const char* why, uint32_t delayMs, bool ledOff) {
  LOG1("restart: %s (after %lums)", why, (unsigned long)delayMs);
  if (ledOff) ledSet(LED_OFF);  // SK6812 は給電されている間は色を保持する
  Serial.flush();
  delay(delayMs);
  ESP.restart();
}

// S/PDIF が無いときは BT 接続しない: 正常が TX_BT_START_OK_MS 続いたら A2DP を始め、始めた後にエラーが
// TX_BT_STOP_ERR_MS 続いたら再起動して待ちに戻る（RX は TX の切断で再起動し、スマホ等がつなげるようになる）
static void btGate() {
#if TX_BT_NEED_SPDIF
  static uint32_t since = 0;  // 今の状態（始める前は正常、始めた後はエラー）が始まった時刻（0: その状態でない）
  uint32_t now = millis();
  if (!txAudioBtStarted()) {
    if (s_error) {
      if (since != 0) LOG1("bt: waiting for S/PDIF");
      since = 0;
      return;
    }
    if (since == 0) since = now | 1;
    if (now - since >= TX_BT_START_OK_MS) {
      LOG1("bt: S/PDIF ok for %d ms -> start A2DP", TX_BT_START_OK_MS);
      since = 0;
      txAudioStartBt();
    }
  } else {
    if (!s_error) {
      since = 0;
      return;
    }
    if (since == 0) since = now | 1;
    if (now - since >= TX_BT_STOP_ERR_MS) {
      restart("no S/PDIF for TX_BT_STOP_ERR_MS: stop BT and wait", 5, false);
    }
  }
#endif
}

void txSetup() {
  // G39 は外付けのプルアップで、押すと L（M5Unified も GPIO.in1 を反転して読む）。入力専用なので pinMode は要らない
  s_ignoreRelease = (digitalRead(PIN_BUTTON) == LOW);
  if (s_ignoreRelease) LOG1("button held at boot: ignore the first release");
  bool keep = (esp_reset_reason() == ESP_RST_SW && s_rtcMagic == kRtcMagic);
  setSimError(keep && s_rtcSimError);
  if (s_simError) LOG1("simulated error: ON (kept over restart)");
  txAudioSetup();
#if TX_BT_NEED_SPDIF
  LOG1("bt: start A2DP after S/PDIF ok for %d ms, stop after no S/PDIF for %d ms", TX_BT_START_OK_MS,
       TX_BT_STOP_ERR_MS);
#else
  txAudioStartBt();
#endif
  updateError();
  updateLed();
}

void txLoop() {
  // ボタン（G39）: TX_LONG_PRESS_MS 以上で再起動（模擬エラーも消す）、それより短い押下（離したとき）は模擬エラーの切り替え
  if (M5.BtnA.pressedFor(TX_LONG_PRESS_MS)) {
    setSimError(false);
    restart("button long press", 5, true);
  }
  if (M5.BtnA.wasReleased() && s_ignoreRelease) {
    s_ignoreRelease = false;
    LOG1("button released (held since boot): ignored");
  }
#if TX_SIM_ERROR_BUTTON
  else if (M5.BtnA.wasReleased()) {
    setSimError(!s_simError);
    LOG1("simulated error: %s", s_simError ? "ON" : "OFF");
  }
#endif

  // 接続状態（コールバックが記録したものを読む）
  int state = txAudioConnState();
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED && !s_connected) {
    s_connected = true;
    char addr[18];
    txAudioPeerAddress(addr, sizeof(addr));
    LOG1("connected: %s after %lu ms from start()", addr, (unsigned long)(millis() - txAudioStartMs()));
    txAudioRestartTone();
  }
  // 一度接続した後の切断だけで再起動する（起動直後の接続試行の失敗でも DISCONNECTED が通知されるため）。
  // 未接続の間の再試行はライブラリの自動再接続に任せる
  if (txAudioWasConnected() && state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    s_connected = false;
    updateLed();
    restart("disconnected", TX_RESTART_DELAY_MS, false);
  }

  updateError();
  btGate();
  updateLed();
  txAudioLoop();
}
