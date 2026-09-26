// atom_test.ino — AtomLite TX/RX 共通ファームウェアの試作
// 仕様: BT_SPEAKER/docs/software.md、ピン: BT_SPEAKER/docs/hardware.md
#include <M5Unified.h>
#include "config.h"
#include "role.h"
#include "dial_link.h"
#include "status_led.h"
#include "rx_audio.h"
#include "tx_audio.h"

static int s_role;

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
      LOG1("stub: SLEEP");
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
  LOG1("atom_test start: role=%s", s_role == ROLE_RX ? "RX" : "TX");

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
