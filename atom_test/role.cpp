// role.cpp — TX/RX の判定
#include "role.h"
#include "config.h"

int detectRole() {
  // 今はビルド設定の ROLE を返すだけ。
  // 将来は G25（PIN_ROLE）を読む実装に差し替える（hardware.md 3節）:
  //   pinMode(PIN_ROLE, INPUT);  // 外付け10kΩでプル済み。内部プルは使わない
  //   return digitalRead(PIN_ROLE) ? ROLE_RX : ROLE_TX;  // H=RX / L=TX
  // M5.begin() は In_I2C（SCL=G21 / SDA=G25）を初期化するため、G25 は必ずその前に読む。
  return ROLE;
}
