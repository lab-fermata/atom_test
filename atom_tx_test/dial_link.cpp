// dial_link.cpp — ログ（"#" 行）だけの最小版（dial_link.h 先頭のコメント参照）
// 元: BT_SPEAKER/atom_a2dp/dial_link.cpp（54971ea）の dialBegin()・dialLog()。dialLog() は変更なし
#include <Arduino.h>
#include <stdarg.h>
#include "dial_link.h"

void dialBegin(bool enableUart) {
  (void)enableUart;      // TX は Serial1（M5Dial との UART）を開かない
  Serial.begin(115200);  // UART0（ATOM Lite の USB シリアル）。atom_a2dp では DIAL_BAUD（config.h の RX の節）
}

void dialLog(const char* fmt, ...) {
  char buf[200];
  buf[0] = '#';
  buf[1] = ' ';
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + 2, sizeof(buf) - 3, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t len = 2 + ((size_t)n < sizeof(buf) - 3 ? (size_t)n : sizeof(buf) - 4);
  buf[len++] = '\n';
  Serial.write(reinterpret_cast<const uint8_t*>(buf), len);  // USB のみ
}
