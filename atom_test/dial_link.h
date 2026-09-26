// dial_link.h — M5Dial との行単位プロトコル（software.md 5節）
//
// - 115200bps 8N1、UTF-8。1行 = 1メッセージ、終端 "\n"（受信時の "\r" は無視）
// - 文字列中の " と \ はバックスラッシュでエスケープする
// - イベント（@...）は USB（Serial）と製品用 UART（Serial1: TX=G32/RX=G26）の両方へ出す
// - コマンドは DIAL_IN_PORT で選んだポートからだけ受信する
// - デバッグログは USB 側にだけ "#" で始まる行として出す
#pragma once
#include <stddef.h>
#include "config.h"

enum DialCmdType {
  CMD_NONE,
  CMD_STATUS,
  CMD_KILL,
  CMD_REBOOT,
  CMD_IR,       // arg にキー
  CMD_UNKNOWN,  // arg に受信行
};

struct DialCmd {
  DialCmdType type;
  char arg[DIAL_LINE_MAX];
};

// enableUart=false なら Serial1 を開かない（TX 役割用）
void dialBegin(bool enableUart);

// 受信ポートを読み、1行そろえばコマンドを解釈して true を返す
bool dialPoll(DialCmd& cmd);

// イベント送信（USB と UART の両方）。文字列引数はエスケープして送る
void dialSendRaw(const char* line);  // "@WOKE" など、引数なしのイベント
void dialSendConnected(const char* mac, const char* peerName);
void dialSendDisconnected();
void dialSendMeta(const char* id, const char* text);

// "#" ログ（USB のみ）。直接呼ばず LOG1/LOG2 を使う
void dialLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG1(...) do { if (LOG_LEVEL >= 1) dialLog(__VA_ARGS__); } while (0)
#define LOG2(...) do { if (LOG_LEVEL >= 2) dialLog(__VA_ARGS__); } while (0)
