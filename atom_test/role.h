// role.h — TX/RX の判定
#pragma once

// ROLE_RX / ROLE_TX（config.h）を返す。M5.begin() より前に呼ぶこと
int detectRole();
