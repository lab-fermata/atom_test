// tx_spdif.h — TX の音声入力: I2S（CS8416 からのスレーブ受信）→ 48k→44.1k の変換 → リングバッファ（software.md 2.1節・2.2節）
// TX_AUDIO_SOURCE=2 では、I2S の代わりに内部で作った正弦波を変換に通す（変換・ドリフトの吸収の試験）
#pragma once
#include <stdint.h>
#include "BluetoothA2DPSource.h"  // Frame

struct SpdifStats {
  uint32_t inFrames;     // 入力（I2S／試験の正弦波）のフレーム数
  uint32_t outFrames;    // 変換してリングバッファに入れたフレーム数
  uint32_t readFrames;   // BT に渡したフレーム数（無音の分を除く）
  uint32_t underruns;    // BT に渡すときに足りなかった回数（エラー中・準備中を除く）
  uint32_t overruns;     // リングバッファがあふれて捨てたフレーム数
  uint32_t ringMin;      // BT に渡す直前のリングバッファの量の最小・最大（フレーム）
  uint32_t ringMax;
  uint32_t procUsMax;    // 1ブロックの変換の最大時間（µs）
  uint32_t restarts;     // エラーから戻ったときに I2S を止めて再開した回数
  int32_t adjPpm;        // 比率の調整（ppm）
  uint32_t rateHz;       // 最後に測った受信レート（Hz。エラー中も測る。TX_AUDIO_SOURCE=1）
  uint32_t rawFrames;    // 診断用: 受け取ったフレーム数（エラー中・時間切れで一部だけの分も含む。TX_AUDIO_SOURCE=1）
  // NVERR（G33）の観察（TX_AUDIO_SOURCE=1）
  bool nverr;            // 今のレベル（true=H）
  uint32_t nverrEdges;   // 変化の回数
  uint32_t nverrHighMs;  // H だった時間の合計（ms）
  uint32_t nverrMinHUs;  // いちばん短い H のパルスの幅（µs。0: H のパルスが終わらなかった）
};

void spdifBegin();                           // I2S（または試験の入力）と受信タスクを始める
int32_t spdifRead(Frame* data, int32_t len); // BT タスク（onFrames）から。len フレームを必ず埋める（足りない分は無音）
bool spdifError();                           // 受信タイムアウト・受信レートの外れ・NVERR の H で、エラー中（復帰の待ちを含む）
const char* spdifErrorReason();              // "ok" / "timeout" / "rate" / "nverr"
void spdifTakeStats(SpdifStats* s);          // 前回からの計測を取り出す（loop() から）
bool spdifNverr();                           // NVERR（G33）の今のレベル（true=H。TX_AUDIO_SOURCE=1 以外は false）
