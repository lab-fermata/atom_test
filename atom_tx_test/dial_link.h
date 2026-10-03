// dial_link.h — ログ（"#" 行）だけの最小版
// 元: BT_SPEAKER/atom_a2dp/dial_link.h（54971ea）。atom_a2dp の dial_link は M5Dial との UART・コマンドの解釈を含み、
// config.h の RX の節のマクロ（EQ_BANDS・DIAL_LINE_MAX・DEBUG_CMD など）が要るため、TX が使う dialBegin()・dialLog()・
// LOG1/LOG2 だけを同じ名前・シグネチャで残した。合体のときは atom_a2dp の dial_link をそのまま使う
//
// - デバッグログは USB 側にだけ "#" で始まる行として出す
#pragma once
#include "config.h"

// enableUart=false なら Serial1 を開かない（TX 役割用）。最小版では USB（Serial）だけを開き、enableUart は使わない
void dialBegin(bool enableUart);

// "#" ログ（USB のみ）。直接呼ばず LOG1/LOG2 を使う
void dialLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG1(...) do { if (LOG_LEVEL >= 1) dialLog(__VA_ARGS__); } while (0)
#define LOG2(...) do { if (LOG_LEVEL >= 2) dialLog(__VA_ARGS__); } while (0)
