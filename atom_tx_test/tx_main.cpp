// tx_main.cpp — TX の役割の入口: 状態の管理、LED、ボタン、切断時の再起動（atom_tx_test_instructions.md 4.1節）、
// BT 接続を始める時機と、S/PDIF が無いときは BT 接続しない（TX_BT_NEED_SPDIF）
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

// 模擬エラーと再起動の理由は、ソフトウェアの再起動（ESP.restart()）をまたいで覚えておく（模擬エラーは S/PDIF が
// 途絶えたまま再起動した状態を模擬するため）。電源投入では消す。長押しの再起動では模擬エラーも消す
static constexpr uint32_t kRtcMagic = 0x53504446;  // "SPDF"
RTC_NOINIT_ATTR static uint32_t s_rtcMagic;
RTC_NOINIT_ATTR static uint32_t s_rtcSimError;
RTC_NOINIT_ATTR static uint32_t s_rtcBoot;  // 次の起動のときの BT の始め方（BootMode）

// BT 接続を始める時機（config.h の「BT 接続を始める時機」）
enum BootMode : uint32_t {
  BOOT_NORMAL = 0,          // すぐ始める（電源投入・長押しの再起動）
  BOOT_AFTER_DISCONNECT,    // 起動から TX_RECONNECT_HOLD_MS 待ってから始める（切断で再起動した）
  BOOT_WAIT_SPDIF,          // S/PDIF の正常が TX_BT_START_OK_MS 続いてから始める（S/PDIF が無いので再起動した）
};
static BootMode s_boot = BOOT_NORMAL;

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

// next: 次の起動のときの BT の始め方
static void restart(const char* why, uint32_t delayMs, bool ledOff, BootMode next) {
  s_rtcBoot = next;
  s_rtcMagic = kRtcMagic;
  LOG1("restart: %s (after %lums)", why, (unsigned long)delayMs);
  if (ledOff) ledSet(LED_OFF);  // SK6812 は給電されている間は色を保持する
  Serial.flush();
  delay(delayMs);
  ESP.restart();
}

// BT 接続を始める時機（s_boot）と、始めた後に S/PDIF が TX_BT_STOP_ERR_MS 無ければ再起動して S/PDIF を待つ
static void btGate() {
  // 今の状態（始める前は正常、始めた後はエラー）が始まった時刻。記録しているかは timing で持つ
  // （以前は 0 を「未記録」にするため since = now | 1 としていたが、now が偶数だと since が now より 1 先になり、
  //  now - since が桁あふれして、待たずに始める・再起動するバグがあった）
  static uint32_t since = 0;
  static bool timing = false;
  uint32_t now = millis();
  if (!txAudioBtStarted()) {
    if (s_boot == BOOT_NORMAL) {
      LOG1("bt: start A2DP (now %lu)", (unsigned long)now);
      txAudioStartBt();
    } else if (s_boot == BOOT_AFTER_DISCONNECT) {
      if ((int32_t)(now - (uint32_t)TX_RECONNECT_HOLD_MS) >= 0) {  // 起動直後なので符号付きで比べてよい（0 でも警告を出さない）
        LOG1("bt: %d ms after restart on disconnect -> start A2DP (now %lu)", TX_RECONNECT_HOLD_MS, (unsigned long)now);
        txAudioStartBt();
      }
    } else {  // BOOT_WAIT_SPDIF
      if (s_error) {
        if (timing) LOG1("bt: waiting for S/PDIF");
        timing = false;
        return;
      }
      if (!timing) {
        timing = true;
        since = now;
      }
      if (now - since >= TX_BT_START_OK_MS) {
        LOG1("bt: S/PDIF ok for %d ms -> start A2DP (ok since %lu, now %lu)", TX_BT_START_OK_MS, (unsigned long)since,
             (unsigned long)now);
        timing = false;
        txAudioStartBt();
      }
    }
    return;
  }
#if TX_BT_NEED_SPDIF
  if (!s_error) {
    timing = false;
    return;
  }
  if (!timing) {
    timing = true;
    since = now;
  }
  if (now - since >= TX_BT_STOP_ERR_MS) {
    restart("no S/PDIF for TX_BT_STOP_ERR_MS: stop BT and wait for S/PDIF", 5, false, BOOT_WAIT_SPDIF);
  }
#endif
}

void txSetup() {
  // G39 は外付けのプルアップで、押すと L（M5Unified も GPIO.in1 を反転して読む）。入力専用なので pinMode は要らない
  s_ignoreRelease = (digitalRead(PIN_BUTTON) == LOW);
  if (s_ignoreRelease) LOG1("button held at boot: ignore the first release");
  bool keep = (esp_reset_reason() == ESP_RST_SW && s_rtcMagic == kRtcMagic);
  s_boot = (keep && s_rtcBoot <= BOOT_WAIT_SPDIF) ? (BootMode)s_rtcBoot : BOOT_NORMAL;
  s_rtcBoot = BOOT_NORMAL;  // 次にどの再起動かを書かずに再起動したら（例外など）すぐ始める
  setSimError(keep && s_rtcSimError);
  if (s_simError) LOG1("simulated error: ON (kept over restart)");
  txAudioSetup();
  static const char* const kBootName[] = {"start now", "hold after disconnect", "wait for S/PDIF"};
  LOG1("bt: boot mode %s (hold %d ms, S/PDIF ok %d ms, stop after no S/PDIF %d ms)", kBootName[s_boot],
       TX_RECONNECT_HOLD_MS, TX_BT_START_OK_MS, TX_BT_STOP_ERR_MS);
  updateError();
  updateLed();
}

void txLoop() {
  // ボタン（G39）: TX_LONG_PRESS_MS 以上で再起動（模擬エラーも消す）、それより短い押下（離したとき）は模擬エラーの切り替え
  if (M5.BtnA.pressedFor(TX_LONG_PRESS_MS)) {
    setSimError(false);
    restart("button long press", 5, true, BOOT_NORMAL);
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
    restart("disconnected", TX_RESTART_DELAY_MS, false, BOOT_AFTER_DISCONNECT);
  }

  updateError();
  btGate();
  updateLed();
  txAudioLoop();
}
