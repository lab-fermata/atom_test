// tx_main.h — TX の役割の入口（atom_tx_test.ino の分岐から呼ぶ。atom_a2dp_instructions.md 1節）
#pragma once

void txSetup();  // 役割の判定と M5.begin()・共通部の後に呼ぶ
void txLoop();
