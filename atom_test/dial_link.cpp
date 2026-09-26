// dial_link.cpp — M5Dial との行単位プロトコル（software.md 5節）
#include <Arduino.h>
#include <stdarg.h>
#include "dial_link.h"

static bool s_uart = false;
static char s_rxBuf[DIAL_LINE_MAX];
static size_t s_rxLen = 0;
static bool s_rxOverflow = false;

static Stream& inPort() {
#if DIAL_IN_PORT == DIAL_PORT_UART
  return Serial1;
#else
  return Serial;
#endif
}

void dialBegin(bool enableUart) {
  Serial.begin(DIAL_BAUD);  // UART0（ATOM Lite の USB シリアル）
  s_uart = enableUart;
  if (s_uart) {
#if DIAL_IN_PORT == DIAL_PORT_UART
    Serial1.begin(DIAL_BAUD, SERIAL_8N1, PIN_DIAL_RX, PIN_DIAL_TX);
#else
    // 受信しないので RX ピンを割り当てない（未接続の G32 のノイズを UART に入れないため）。
    // rxPin=-1 は HardwareSerial::begin / uartSetPins で「割り当てなし」として扱われる。
    Serial1.begin(DIAL_BAUD, SERIAL_8N1, -1, PIN_DIAL_TX);
#endif
  }
}

// ---- 送信 ----

static void writeLine(const char* s, size_t n) {
  // 1回の write で出す（HardwareSerial::write は呼び出し単位でロックされるため、行が混ざらない）
  Serial.write(reinterpret_cast<const uint8_t*>(s), n);
  if (s_uart) Serial1.write(reinterpret_cast<const uint8_t*>(s), n);
}

// " と \ をエスケープして dst に追記する。制御文字（0x00-0x1F）は行の区切りを壊すので空白に置き換える。
// 戻り値は追記後の長さ。入りきらない分は捨てる（UTF-8 の途中で切らないよう、文字単位で判定する）
static size_t appendEscaped(char* dst, size_t len, size_t cap, const char* src) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
  while (*p) {
    // UTF-8 の1文字分のバイト数
    size_t clen = (*p < 0x80) ? 1 : (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    size_t need = (*p == '"' || *p == '\\') ? 2 : clen;
    if (len + need >= cap) break;
    if (*p == '"' || *p == '\\') {
      dst[len++] = '\\';
      dst[len++] = *p++;
    } else if (*p < 0x20) {
      dst[len++] = ' ';
      p++;
    } else {
      for (size_t i = 0; i < clen && *p; i++) dst[len++] = *p++;
    }
  }
  dst[len] = '\0';
  return len;
}

static size_t appendRaw(char* dst, size_t len, size_t cap, const char* src) {
  while (*src && len + 1 < cap) dst[len++] = *src++;
  dst[len] = '\0';
  return len;
}

void dialSendRaw(const char* line) {
  char buf[64];
  size_t n = appendRaw(buf, 0, sizeof(buf) - 1, line);
  buf[n++] = '\n';
  writeLine(buf, n);
}

void dialSendConnected(const char* mac, const char* peerName) {
  char buf[256];
  size_t n = 0;
  const size_t cap = sizeof(buf) - 3;  // 末尾の "}\n" の分を残す
  n = appendRaw(buf, n, cap, "@CONNECTED{\"");
  n = appendEscaped(buf, n, cap, mac);
  n = appendRaw(buf, n, cap, "\",\"");
  n = appendEscaped(buf, n, cap, peerName);
  n = appendRaw(buf, n, cap, "\"");
  buf[n++] = '}';
  buf[n++] = '\n';
  writeLine(buf, n);
}

void dialSendDisconnected() {
  dialSendRaw("@DISCONNECTED");
}

void dialSendMeta(const char* id, const char* text) {
  char buf[META_TEXT_MAX * 2 + 32];  // 全文字がエスケープされても入る大きさ
  size_t n = 0;
  const size_t cap = sizeof(buf) - 3;
  n = appendRaw(buf, n, cap, "@META{\"");
  n = appendEscaped(buf, n, cap, id);
  n = appendRaw(buf, n, cap, "\",\"");
  n = appendEscaped(buf, n, cap, text);
  n = appendRaw(buf, n, cap, "\"");
  buf[n++] = '}';
  buf[n++] = '\n';
  writeLine(buf, n);
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

// ---- 受信 ----

// バックスラッシュエスケープを解除する（その場で書き換え）
static void unescape(char* s) {
  char* w = s;
  for (char* r = s; *r; r++) {
    if (*r == '\\' && r[1] != '\0') r++;
    *w++ = *r;
  }
  *w = '\0';
}

static void parse(char* line, DialCmd& cmd) {
  unescape(line);
  cmd.arg[0] = '\0';
  size_t len = strlen(line);
  if (strcmp(line, "STATUS") == 0) {
    cmd.type = CMD_STATUS;
  } else if (strcmp(line, "SLEEP") == 0) {
    cmd.type = CMD_SLEEP;
  } else if (strcmp(line, "KILL") == 0) {
    cmd.type = CMD_KILL;
  } else if (strcmp(line, "REBOOT") == 0) {
    cmd.type = CMD_REBOOT;
  } else if (len >= 4 && strncmp(line, "IR{", 3) == 0 && line[len - 1] == '}') {
    cmd.type = CMD_IR;
    size_t klen = len - 4;
    memcpy(cmd.arg, line + 3, klen);
    cmd.arg[klen] = '\0';
  } else {
    cmd.type = CMD_UNKNOWN;
    strlcpy(cmd.arg, line, sizeof(cmd.arg));
  }
}

bool dialPoll(DialCmd& cmd) {
  Stream& in = inPort();
  while (in.available() > 0) {
    int c = in.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      bool overflow = s_rxOverflow;
      s_rxBuf[s_rxLen] = '\0';
      s_rxLen = 0;
      s_rxOverflow = false;
      if (overflow) {
        LOG1("line too long, dropped");
        continue;
      }
      if (s_rxBuf[0] == '\0') continue;  // 空行は無視
      parse(s_rxBuf, cmd);
      return true;
    }
    if (s_rxLen < sizeof(s_rxBuf) - 1) {
      s_rxBuf[s_rxLen++] = (char)c;
    } else {
      s_rxOverflow = true;
    }
  }
  cmd.type = CMD_NONE;
  return false;
}
