// dial_link.cpp — ログ（"#" 行）だけの最小版（dial_link.h 先頭のコメント参照）
// 変えた点: 行の先頭に millis() を付けた（dialLog）
// 元: BT_SPEAKER/atom_a2dp/dial_link.cpp（54971ea）の dialBegin()・dialLog()
#include <Arduino.h>
#include <stdarg.h>
#include "dial_link.h"

void dialBegin(bool enableUart) {
  (void)enableUart;      // TX は Serial1（M5Dial との UART）を開かない
  Serial.begin(115200);  // UART0（ATOM Lite の USB シリアル）。atom_a2dp では DIAL_BAUD（config.h の RX の節）
}

// atom_tx_test: 行の先頭に TX の側の時刻（millis()、ms）を付ける（パソコンで受け取った時刻とは別に、TX の中の順序と
// 間隔を見るため。README の「S/PDIF 機器をつないだ確認」）
void dialLog(const char* fmt, ...) {
  char buf[220];
  int h = snprintf(buf, sizeof(buf), "# %lu ", (unsigned long)millis());
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + h, sizeof(buf) - h - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t len = h + ((size_t)n < sizeof(buf) - h - 1 ? (size_t)n : sizeof(buf) - h - 2);
  buf[len++] = '\n';
  Serial.write(reinterpret_cast<const uint8_t*>(buf), len);  // USB のみ
}
