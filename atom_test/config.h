// config.h — ビルド設定とピン定義
// ピン割当の正本は BT_SPEAKER/docs/hardware.md 3節
#pragma once

// ---- ビルド設定（arduino-cli の --build-property "compiler.cpp.extra_flags=-DXXX=..." でも上書きできる）----

// 役割（将来は G25 の判定ジャンパで決める。role.cpp 参照）
#define ROLE_RX 0
#define ROLE_TX 1
#ifndef ROLE
#define ROLE ROLE_RX
#endif

// M5Dial からのコマンドを受信するポート（送信は常に両方へ出す）
#define DIAL_PORT_USB  0  // Serial（UART0、USB）
#define DIAL_PORT_UART 1  // Serial1（Grove、TX=G26/RX=G32）
#ifndef DIAL_IN_PORT
#define DIAL_IN_PORT DIAL_PORT_USB
#endif

// 1: A2DP切断を検知したら ESP.restart() する（impl-notes 1.3）。0: 再起動せずログを追える
#ifndef RESTART_ON_DISCONNECT
#define RESTART_ON_DISCONNECT 1
#endif

// 0: なし / 1: 通常 / 2: 詳細（USB側だけに "#" 行で出す）
#ifndef LOG_LEVEL
#define LOG_LEVEL 1
#endif

// ---- ピン（hardware.md 3節）----
#define PIN_I2S_BCK   22  // RX: → PCM5122 BCK   / TX: ← CS8416 OSCLK
#define PIN_I2S_WS    19  // RX: → PCM5122 LRCK  / TX: ← CS8416 OLRCK
#define PIN_I2S_DATA  23  // RX: → PCM5122 DIN   / TX: ← CS8416 SDOUT
#define PIN_DIAL_TX   26  // RX: UART TXD → M5Dial（Grove 黄）
#define PIN_DIAL_RX   32  // RX: UART RXD ← M5Dial（Grove 白）
#define PIN_RERR      33  // TX: ← CS8416 RERR
#define PIN_IR_RX     33  // RX: ← Unit IR
#define PIN_ROLE      25  // 判定ジャンパ（H=RX / L=TX）
#define PIN_IR_TX     21  // RX: → Unit IR
#define PIN_LED       27  // 本体 RGB LED（M5.Led が使う）
#define PIN_BUTTON    39  // 本体ボタン

// ---- その他 ----
#define DIAL_BAUD        115200
#define DIAL_LINE_MAX    160  // 受信1行の最大バイト数（超えた行は捨てる）
#define BT_DEVICE_NAME   "fermata BT Speaker"
#define META_TEXT_MAX    256  // @META 1項目の最大バイト数（UTF-8、超えた分は切り捨て）

#define LED_LEVEL        64   // 点灯時の輝度（0-255。本体LEDは明るいので控えめ）
#define LED_BLINK_MS     1000 // ゆっくり点滅の半周期
