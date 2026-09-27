// atom_test.ino — AtomLite TX/RX 共通ファームウェアの試作
// 仕様: BT_SPEAKER/docs/software.md、ピン: BT_SPEAKER/docs/hardware.md
#include <M5Unified.h>
#include "config.h"
#include "role.h"
#include "dial_link.h"
#include "status_led.h"
#include "rx_audio.h"
#include "tx_audio.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>

static int s_role;

// SLEEP: Deep Sleep に入る（software.md 3.2節）。起床後は setup() から通常どおり起動し、@WOKE を送る
static void enterDeepSleep() {
  LOG1("SLEEP: deep sleep (wake: G26 low%s)", SLEEP_WAKE_BUTTON ? " or button" : "");
  // SK6812 は給電されている間は色を保持するので、消してから眠る
  ledSet(LED_OFF);
  delay(5);
  Serial.flush();
  Serial1.flush();
  Serial1.end();
  // ext0: G26 の Low で起床する。M5Dial がつながっていないときに浮かないよう、RTC 側でプルアップする
  // （ext0 を使うと RTC ペリフェラルの電源が入ったままになり、RTC 側のプルが効く）
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_DIAL_RX, 0);
  rtc_gpio_pullup_en((gpio_num_t)PIN_DIAL_RX);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_DIAL_RX);
#if SLEEP_WAKE_BUTTON
  // ext1: 本体ボタン（G39、外付け 4.7kΩ でプルアップ）。ESP32 の ext1 は ALL_LOW か ANY_HIGH
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_BUTTON, ESP_EXT1_WAKEUP_ALL_LOW);
#endif
  esp_deep_sleep_start();
}

static void handleCommand(const DialCmd& cmd) {
  switch (cmd.type) {
    case CMD_STATUS:
      rxSendStatus();
      break;
    case CMD_KILL:
      rxKill();
      break;
    case CMD_REBOOT:
      LOG1("REBOOT");
      Serial.flush();
      Serial1.flush();
      ESP.restart();
      break;
    case CMD_SLEEP:
      enterDeepSleep();
      break;
    case CMD_IR:
      LOG1("stub: IR{%s}", cmd.arg);
      break;
    case CMD_UNKNOWN:
      LOG1("unknown: %s", cmd.arg);
      break;
    default:
      break;
  }
}

void setup() {
  // 0. Deep Sleep からの起床なら、ext0/ext1 に使ったピンは RTC IO のままなので通常の GPIO に戻す
  //    （戻さないと Serial1 の RX に使えない）
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0 || wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
    rtc_gpio_deinit((gpio_num_t)PIN_DIAL_RX);
    rtc_gpio_deinit((gpio_num_t)PIN_BUTTON);
  }

  // 1. 役割の判定（G25 は M5.begin() より前に読む。hardware.md 3節）
  s_role = detectRole();

  // 2. M5.begin()
  auto cfg = M5.config();
  // ATOM Lite には IMU/RTC が無い。有効のままだと In_I2C（SCL=G21 / SDA=G25）でプローブが走り、
  // RX では G21（IR_TX）にクロックが出るため無効にする（M5Unified.inl の _begin_rtc_imu で確認）
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  M5.begin(cfg);

  // 3. 共通部
  dialBegin(s_role == ROLE_RX);
  ledBegin();
  LOG1("atom_test start: role=%s, wakeup cause=%d", s_role == ROLE_RX ? "RX" : "TX", (int)wakeCause);

  // 4. 役割ごとの初期化
  if (s_role == ROLE_RX) {
    rxSetup();
    dialSendRaw("@WOKE");
  } else {
    txSetup();
  }
}

void loop() {
  M5.update();
  ledUpdate();
  if (s_role == ROLE_RX) {
    DialCmd cmd;
    if (dialPoll(cmd)) handleCommand(cmd);
    rxLoop();
  } else {
    txLoop();
  }
  delay(1);
}
