// role.cpp — TX/RX の判定（software.md 1節、hardware.md 3節）
// コピー元: BT_SPEAKER/atom_a2dp/role.cpp（c1d9300）。変更なし（atom_tx_test では config.h の既定 ROLE_TX で G25 を読まない）
#include <Arduino.h>
#include "role.h"
#include "config.h"

int detectRole() {
#if ROLE == ROLE_AUTO
  // G25 を内部プルアップの入力にして読む。開放・外部のプルアップなら H（RX）、GND に落としていれば L（TX）。
  // M5.begin() は In_I2C（SCL=G21 / SDA=G25）を初期化するため、必ずその前に読む
  pinMode(PIN_ROLE, INPUT_PULLUP);
  delayMicroseconds(ROLE_SETTLE_US);
  return digitalRead(PIN_ROLE) ? ROLE_RX : ROLE_TX;
#else
  return ROLE;  // ビルドの設定で固定（G25 は読まない）
#endif
}
