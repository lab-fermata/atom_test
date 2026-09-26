// tx_audio.cpp — TX（A2DP Source）。未実装のプレースホルダ
// 仕様は BT_SPEAKER/docs/software.md 2節
#include <Arduino.h>
#include "tx_audio.h"
#include "dial_link.h"
#include "status_led.h"

void txSetup() {
  LOG1("TX: not implemented");
  // TODO(2.1): I2S をスレーブ受信で初期化する（BCK=G22 / WS=G19 / DATA=G23 は CS8416 からの入力。I2S 24bit 想定 [OI-01]）
  // TODO(2.1): 48kHz → 44.1kHz のサンプルレート変換。バッファ量を監視してクロックドリフトを吸収する
  // TODO(2.2): G33（RERR）を INPUT で初期化する（内部プルは使わない。hardware.md 4.2節）
  // TODO(2.3): A2DP Source を開始する。set_auto_reconnect(true)、接続先は "fermata BT Speaker"
  // TODO(2.3): AVRCP Target として接続に応じ、常に空メタデータを返す（software.md 1節）
  ledSet(LED_BLUE_BLINK);  // TODO(2.4): 接続状態に合わせる
}

void txLoop() {
  // TODO(2.2): RERR = H または I2S 受信タイムアウト（目安20ms [OI-24]）の間は受信データを捨てて無音を送る
  // TODO(2.2): 復帰時は I2S ドライバを停止→再起動してから送出を再開する [OI-05]
  // TODO(2.4): LED 接続待ち=青点滅 / 接続中=青点灯 / エラー検出中=赤 / 接続中かつエラー=混色 [OI-20]
}
