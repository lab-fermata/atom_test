// tx_audio.h — TX（A2DP Source）。atom_tx_test ではダミーの音声（正弦波）を送る
#pragma once
#include <stdint.h>

void txAudioSetup();        // 音源・Source の準備（BT はまだ始めない）
void txAudioStartBt();      // A2DP を始める（start()。1回だけ）
bool txAudioBtStarted();
void txAudioLoop();         // 掃引の周波数の切り替え、1秒ごとの計測ログ
void txAudioRestartTone();  // 掃引を最初の周波数からやり直す（接続したときに呼ぶ）

int txAudioConnState();          // 最後に通知された接続状態（esp_a2d_connection_state_t）
bool txAudioWasConnected();      // 起動後に一度でも CONNECTED になったか
const char* txAudioStateName(int state);
uint32_t txAudioStartMs();       // start() を呼んだ時刻（millis()）
void txAudioPeerAddress(char* buf, int size);  // 接続先のアドレス（"XX:XX:…"）

void txAudioSetError(bool error);    // エラー中（S/PDIF または模擬エラー）。無音（TX_MUTE_ON_ERROR）と AVRCP の通知に使う
bool txAudioSpdifError();            // S/PDIF（I2S）のエラーを検出中か（TX_AUDIO_SOURCE=1）
const char* txAudioSpdifReason();    // "ok" / "timeout" / "rerr"
