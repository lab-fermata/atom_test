// role.h — TX/RX の判定
// コピー元: BT_SPEAKER/atom_a2dp/role.h（c1d9300）。変更なし
#pragma once

// ROLE_RX / ROLE_TX（config.h）を返す。M5.begin() より前に呼ぶこと
int detectRole();
