// atom_tx_test.ino — TX 側 AtomLite の機能テスト（BT 接続・A2DP Source。音声は正弦波のダミー）
// コピー元: BT_SPEAKER/atom_a2dp/atom_a2dp.ino（c1d9300）の TX の分岐
// 変えた点: Deep Sleep からの起床の処理（RX だけ）を外した。RX の分岐は「このスケッチに無い」表示だけにした。
//           起動ログから受信ポート（RX）を外した
// 指示書: atom_tx_test_instructions.md。構成: BT_SPEAKER/atom_a2dp_instructions.md
#include <M5Unified.h>
#include "config.h"
#include "role.h"
#include "dial_link.h"
#include "status_led.h"
#include "tx_main.h"

static int s_role;

void setup() {
  // 0. Deep Sleep からの起床の処理は RX だけ（atom_a2dp.ino）。atom_tx_test には無い

  // 1. 役割の判定（atom_tx_test は config.h の既定で ROLE_TX に固定。G25 は読まない）
  s_role = detectRole();

  // 2. M5.begin()
  auto cfg = M5.config();
  // ATOM Lite には IMU/RTC が無い。有効のままだと In_I2C（SCL=G21 / SDA=G25）でプローブが走り、
  // RX では G21（IR_TX）にクロックが出るため無効にする（M5Unified.inl の _begin_rtc_imu で確認）
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  M5.begin(cfg);

  // 3. 共通部（UART の Serial1 は RX だけ。TX は外部インターフェースを持たない）
  dialBegin(s_role == ROLE_RX);
  ledBegin();
  LOG1("atom_tx_test start: role=%s (%s)", s_role == ROLE_RX ? "RX" : "TX", ROLE == ROLE_AUTO ? "G25" : "fixed");

  // 4. 役割ごとの初期化
  if (s_role == ROLE_RX) {
    ledSet(LED_RED);
    LOG1("RX is not in this sketch");
  } else {
    txSetup();
  }
}

void loop() {
  M5.update();
  ledUpdate();
  if (s_role == ROLE_TX) {
    txLoop();
  }
  delay(1);
}
