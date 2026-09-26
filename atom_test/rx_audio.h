// rx_audio.h — RX（A2DP Sink）
#pragma once

void rxSetup();
void rxLoop();
void rxSendStatus();  // STATUS への応答
void rxKill();        // KILL: Bluetooth を切断する
