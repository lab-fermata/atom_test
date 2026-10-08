// config.h — ビルド設定とピン定義（共通・RX・TX の節に分ける。atom_a2dp_instructions.md 1節）
// コピー元: BT_SPEAKER/atom_a2dp/config.h（c0b5439）
// 変えた点: ROLE の既定を ROLE_TX にした。BT_DEVICE_NAME を RX の節から共通の節に移した（TX も接続先として使う）。
//           RX の節はコメントだけにした。TX の節を書いた
// ピン割当の正本は BT_SPEAKER/docs/hardware.md 3節
#pragma once

// audio-tools も LOG_LEVEL（AudioToolsConfig.h）と PIN_I2S_BCK・PIN_I2S_WS（PlatformConfig/esp32.h）を
// 無条件または #ifndef で定義しており、本ファイルの同名マクロとぶつかる。
// - AudioTools.h を後から include すると、PIN_I2S_BCK/WS が 14/15 に置き換わり、LOG_LEVEL（int）が
//   audio-tools の LogLevel（enum）の初期値に入ってコンパイルエラーになる
// - AudioTools.h を先に include すると、LOG_LEVEL が AudioLogger::Warning のままになる
// そのため AudioTools.h を使うファイル（rx_audio.cpp・dsp.cpp）では、AudioTools.h・BluetoothA2DPSink.h の後で
// #undef してから本ファイルを include する。同じ理由で LOG_LEVEL を -D で上書きすることはできない（本ファイルを書き換える）
#if defined(LOG_LEVEL) || defined(PIN_I2S_BCK) || defined(PIN_I2S_WS)
#error "LOG_LEVEL / PIN_I2S_BCK / PIN_I2S_WS already defined (audio-tools?). #undef them before including config.h"
#endif

// ビルド設定は arduino-cli の --build-property "compiler.cpp.extra_flags=-DXXX=..." でも上書きできる（LOG_LEVEL を除く）

// ================================================================
// 共通
// ================================================================

// 役割。ROLE_AUTO: 起動時に G25 を読む（内部プルアップ。開放・H=RX、L=TX）。ROLE_RX／ROLE_TX: G25 を読まずに固定
// atom_tx_test は TX の試験用なので、既定を ROLE_TX にする（atom_a2dp の既定は ROLE_AUTO）
#define ROLE_RX   0
#define ROLE_TX   1
#define ROLE_AUTO 2
#ifndef ROLE
#define ROLE ROLE_TX
#endif
#define ROLE_SETTLE_US 100  // G25 を内部プルアップにしてから読むまでの待ち（µs）

// 0: なし / 1: 通常 / 2: 詳細（USB側だけに "#" 行で出す）。2 で1秒ごとの計測ログを出す
// （-D では上書きできない。先頭のコメント参照）
#define LOG_LEVEL 2

// ---- ピン（hardware.md 3節）----
#define PIN_I2S_BCK   22  // RX: → PCM5122 BCK   / TX: ← CS8416 OSCLK
#define PIN_I2S_WS    19  // RX: → PCM5122 LRCK  / TX: ← CS8416 OLRCK
#define PIN_I2S_DATA  23  // RX: → PCM5122 DIN   / TX: ← CS8416 SDOUT
#define PIN_ROLE      25  // 判定ジャンパ（H=RX / L=TX）
#define PIN_LED       27  // 本体 RGB LED（M5.Led が使う）
#define PIN_BUTTON    39  // 本体ボタン
#define PIN_NVERR     33  // TX: ← CS8416 NV/RERR（キットはプルダウンで NVERR。H=エラー・アンロック）。エラーの判定とスリープからの起床。RX: IR_RX

#define LED_LEVEL        64   // 点灯時の輝度（0-255。本体LEDは明るいので控えめ）
#define LED_BLINK_MS     1000 // ゆっくり点滅の半周期

// RX が名乗り、TX が接続する名前（software.md 1節）。atom_a2dp では RX の節にある
#define BT_DEVICE_NAME   "fermata BT Speaker"

// ================================================================
// RX
// ================================================================

// atom_tx_test には RX の役割は無い（atom_a2dp/config.h の RX の節を参照）

// ================================================================
// TX
// ================================================================

// 自分の BT 名（RX の @CONNECTED に出るピア名）
#ifndef BT_TX_LOCAL_NAME
#define BT_TX_LOCAL_NAME "fermata SPDIF"
#endif

// 一度接続した後に切断されたら、これだけ待ってから ESP.restart() する（ms）。RX の RESTART_DELAY_MS と同じ
#ifndef TX_RESTART_DELAY_MS
#define TX_RESTART_DELAY_MS 300
#endif

// 本体ボタンをこれ以上押し続けたら、自分を再起動する（ms）
#ifndef TX_LONG_PRESS_MS
#define TX_LONG_PRESS_MS 3000
#endif

// 1: 本体ボタンの短押しで「模擬エラー」（S/PDIF のエラーの代わり。LED を赤にする）を切り替える。0: 外す
#ifndef TX_SIM_ERROR_BUTTON
#define TX_SIM_ERROR_BUTTON 1
#endif

// ---- ダミーの音声（正弦波。44.1kHz・16bit・2ch）----

// 0: 掃引（RX のユーザー EQ のバンド 100〜12500Hz を TONE_STEP_MS ずつ順に）/ 1: TONE_FIXED_HZ に固定
#ifndef TONE_MODE
#define TONE_MODE 0
#endif
#ifndef TONE_FIXED_HZ
#define TONE_FIXED_HZ 1000
#endif
#ifndef TONE_STEP_MS
#define TONE_STEP_MS 3000
#endif
// 振幅（dBFS。-20 でピーク約 3,277）
#ifndef TONE_LEVEL_DBFS
#define TONE_LEVEL_DBFS (-20)
#endif
// 0: L=R / 1: L のみ / 2: R のみ（出さない側は 0）
#ifndef TONE_CHANNEL
#define TONE_CHANNEL 0
#endif
// 正弦波の作り方。0: 1024 点のテーブル＋線形補間 / 1: sinf()（処理時間を比べる用）
#ifndef TONE_USE_SINF
#define TONE_USE_SINF 0
#endif

// ---- 音源 ----

// 0: 正弦波（上の TONE_*）/ 1: I2S（CS8416 からのスレーブ受信 → 変換 → 送信。software.md 2.1節・2.2節）/
// 2: 変換の試験（内部で作った TX_SRC_TEST_FS の正弦波を、I2S の代わりに変換に通す）
#ifndef TX_AUDIO_SOURCE
#define TX_AUDIO_SOURCE 0
#endif

// 1: エラー中（模擬エラーを含む）は無音を送る（software.md 2.2節）。0: 模擬エラーでは音を止めない
#ifndef TX_MUTE_ON_ERROR
#define TX_MUTE_ON_ERROR 1
#endif

// ---- I2S 受信（TX_AUDIO_SOURCE=1）----

// 入力のサンプルレート（Hz。S/PDIF は 48kHz の想定。software.md 2.1節）
#ifndef TX_IN_RATE
#define TX_IN_RATE 48000
#endif
// I2S のスロット幅（bit）。データ幅も同じにして読み、上位 16bit を使う。
// CS8416 の出力は I2S 24bit の想定 [OI-01]。24bit のデータは 32bit スロットの上位に詰めて出る
#ifndef TX_I2S_SLOT_BITS
#define TX_I2S_SLOT_BITS 32
#endif
// 1回に読む量（フレーム。48kHz で 96 = 2ms）
#ifndef TX_I2S_BLOCK_FRAMES
#define TX_I2S_BLOCK_FRAMES 96
#endif
// I2S 受信タイムアウト（ms）。この時間データが来なければエラー [OI-24]
#ifndef TX_I2S_TIMEOUT_MS
#define TX_I2S_TIMEOUT_MS 20
#endif
// エラーが消えてから、これだけ正常が続いたら I2S を止めて再開し、送出を再開する（ms）
#ifndef TX_RECOVER_MS
#define TX_RECOVER_MS 200
#endif
// 受信レート（LRCK）の監視: この時間ごとに測り（ms）、TX_IN_RATE から ±この割合（%）を外れたらエラー。
// CS8416 は信号断で OLRCK が約 2.925kHz になる（クロックの切り替えが有効なら OMCK/256）
#ifndef TX_RATE_WINDOW_MS
#define TX_RATE_WINDOW_MS 500
#endif
#ifndef TX_RATE_TOL_PCT
#define TX_RATE_TOL_PCT 3
#endif

// ---- BT 接続を始める時機（ユーザーの判断、2026-10-06）----
// - 普段の起動（電源投入・長押しの再起動）: S/PDIF を待たずにすぐ A2DP を始める。S/PDIF から音が取れるまでは無音を送る
// - 切断で再起動したとき: 起動から TX_RECONNECT_HOLD_MS は始めない（RX が切ったときに、少なくとも 10 秒はスマホ等が
//   RX につなげるように。RX の電源断なども同じ扱い）。ESP32-A2DP 1.8.11 は start() の中で、最初の接続を試みる前に
//   決め打ちで 10 秒待つ（BluetoothA2DPSource.cpp の av_hdl_stack_evt の delay_ms(10000)）ので、0 でも切断から
//   接続までは約 11 秒空く（ユーザーの判断で 0。2026-10-06）
// - 「S/PDIF が TX_BT_STOP_ERR_MS 無い」で再起動したとき: S/PDIF の正常が TX_BT_START_OK_MS 続くまで始めない
//   （大元の電源が切れているとき、スマホ等が RX につなげるように）。どの再起動かは RTC のメモリで覚える
#ifndef TX_RECONNECT_HOLD_MS
#define TX_RECONNECT_HOLD_MS 0
#endif
// 接続してから、送出の準備の確認（ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY）をこちらから送るまでの待ち（ms）と、
// 送出が始まらないときに送り直す間隔（ms）。0: 送らない（ライブラリの 10 秒周期のハートビートを待つ）
#ifndef TX_MEDIA_KICK_MS
#define TX_MEDIA_KICK_MS 300
#endif
#ifndef TX_MEDIA_KICK_RETRY_MS
#define TX_MEDIA_KICK_RETRY_MS 2000
#endif

// ---- S/PDIF が無いときは BT 接続しない（大元の電源が切れているとき、スマホ等が RX につなげるように）----

// 1: A2DP を始めた後にエラー（模擬エラーを含む）が TX_BT_STOP_ERR_MS 続いたら、ESP.restart() して、S/PDIF の正常が
//    TX_BT_START_OK_MS 続くまで A2DP を始めない（start() の呼び直しはしない。impl-notes.md 1.1節）。
//    0: エラーでも切らない
#ifndef TX_BT_NEED_SPDIF
#define TX_BT_NEED_SPDIF 1
#endif
#ifndef TX_BT_START_OK_MS
#define TX_BT_START_OK_MS 1000
#endif
#ifndef TX_BT_STOP_ERR_MS
#define TX_BT_STOP_ERR_MS 30000
#endif

// ---- S/PDIF を待つ間のディープスリープ（省電力・発熱・不要な電波の対策。ユーザーの判断、2026-10-08）----
// S/PDIF を待っている間（上の「S/PDIF が TX_BT_STOP_ERR_MS 無い」で再起動した後）にエラーなら、LED を消して寝る。
// - NVERR（G33）が H（アンロック）: G33 が L（ロック）になったら起きる（ext0）
// - NVERR が L なのにエラー（DIR の電源断、48kHz 以外でロック、模擬エラー）: 起動から TX_SLEEP_CHECK_MS 待って
//   受信レートで確かめてから寝て、TX_SLEEP_TIMER_MS ごとに起きて確かめる（ext0 は L で起き続けるので使わない）
// - ボタン（G39）でも起きる（ext1）。ボタンで起きたら TX_SLEEP_BUTTON_AWAKE_MS は寝ない（LED も点ける）
// 起きたら「S/PDIF を待つ」起動になる（S/PDIF の正常が TX_BT_START_OK_MS 続いたら A2DP を始める）。TX_AUDIO_SOURCE=1 のときだけ
#ifndef TX_SLEEP_WAIT_SPDIF
#define TX_SLEEP_WAIT_SPDIF 1
#endif
#ifndef TX_SLEEP_TIMER_MS
#define TX_SLEEP_TIMER_MS 5000
#endif
// NVERR が L のときに寝るまでの起動からの時間（ms）。I2S の開始（約 0.4 秒）から受信レートの窓 2 つ（エラーからの
// 復帰の条件）を測り終える約 1.4 秒より長くする
#ifndef TX_SLEEP_CHECK_MS
#define TX_SLEEP_CHECK_MS 2500
#endif
#ifndef TX_SLEEP_BUTTON_AWAKE_MS
#define TX_SLEEP_BUTTON_AWAKE_MS 10000
#endif

// ---- 変換の試験（TX_AUDIO_SOURCE=2）----

// 試験の入力の周波数（Hz）と、入力のクロックのずれ（ppm。+ なら入力が速い）
#ifndef TX_SRC_TEST_HZ
#define TX_SRC_TEST_HZ 440
#endif
#ifndef TX_SRC_TEST_PPM
#define TX_SRC_TEST_PPM 0
#endif

// ---- 変換（48k→44.1k）とリングバッファ ----

// リングバッファ（44.1kHz・16bit・2ch のフレーム数）と、保つ量の目標（ms）。
// 相手が急にいなくなると空きヒープが約 11KB まで減る（README）ので、大きくしすぎない
#ifndef TX_RING_FRAMES
#define TX_RING_FRAMES 2048
#endif
#ifndef TX_RING_TARGET_MS
#define TX_RING_TARGET_MS 25
#endif
// 送出の開始・足りなくなった後は、この量（ms）まで貯めてから BT に渡す（開始の直後に BT が多めに読むため）
#ifndef TX_RING_PRIME_MS
#define TX_RING_PRIME_MS 30
#endif
// 比率の調整の上限（ppm）と強さ（目標からのずれ 100% あたりの ppm）
#ifndef TX_SRC_ADJ_MAX_PPM
#define TX_SRC_ADJ_MAX_PPM 2000
#endif
// 比例の強さ: 5000ppm で、量のずれを約 5 秒（目標の量 ÷ (44.1 フレーム/秒 × 5)）で戻す
#ifndef TX_SRC_ADJ_GAIN_PPM
#define TX_SRC_ADJ_GAIN_PPM 5000
#endif
// 比率の調整の積分の時定数（秒）。クロックのずれの分を積分で吸収し、リングバッファの量を目標に戻す。
// 比例で戻る時間（約 5 秒）より十分長くして、行き過ぎ・揺れを防ぐ（10 秒・比例 1000ppm では約 100 秒周期で揺れた）
#ifndef TX_SRC_ADJ_TI_S
#define TX_SRC_ADJ_TI_S 30
#endif
// リングバッファの量の平滑化の時定数（ms）。BT は数十 ms ごとにまとめて読むので、量が 25ms 分ほど上下する
#ifndef TX_SRC_FILL_TAU_MS
#define TX_SRC_FILL_TAU_MS 500
#endif

// ---- AVRCP（S/PDIF の状態を RX に知らせる）----

// 1: S/PDIF の状態を AVRCP のパススルーのコマンドで RX に送る（正常: TX_PSTH_KEY_OK、エラー: TX_PSTH_KEY_ERR）。0: 送らない
// （ESP-IDF の AVRCP Target は曲名・再生状態の通知を送れないため。tx_audio.cpp 先頭のコメント）
#ifndef TX_AVRCP_STATUS
#define TX_AVRCP_STATUS 1
#endif
#ifndef TX_PSTH_KEY_OK
#define TX_PSTH_KEY_OK 0x44   // ESP_AVRC_PT_CMD_PLAY
#endif
#ifndef TX_PSTH_KEY_ERR
#define TX_PSTH_KEY_ERR 0x45  // ESP_AVRC_PT_CMD_STOP
#endif
// AVRCP の Controller が RX とつながってから、最初に状態を送るまでの待ち（ms。RX がピア名を受け取るのを待つ）
#ifndef TX_PSTH_DELAY_MS
#define TX_PSTH_DELAY_MS 1000
#endif
