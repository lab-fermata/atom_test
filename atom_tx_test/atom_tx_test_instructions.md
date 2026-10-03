# 指示書: atom_tx_test — TX側 AtomLite の機能テスト（BT接続・A2DP）

対象フォルダ: `C:\workspace\MAKER\atom_test\atom_tx_test`（`atom_test` リポジトリ内。スケッチ名 `atom_tx_test`）
作成: 2026-10-03（Claude（チャット）で検討、Claude Code で実装する）
AtomLite（TX）: COM7（書き込みの前に必ず `arduino-cli board list` で確かめる）

## 1. 目的と位置づけ

- TX の AtomLite で **Bluetooth 接続と A2DP（Source）の送信**を確かめる試験用スケッチ。**S/PDIF（I2S）は使わない**。音声は合成した正弦波（ダミー）を送る
- 確認が取れたら、TX の機能を `BT_SPEAKER/atom_a2dp/` に合体する。**そのため、ライブラリ・ファイル構成・命名・設定の書き方は RX（`atom_a2dp`）にそろえ、共用できる部分は `atom_a2dp` のファイルをコピーして使う**
- 製品コードではない。ここで得た知見は、ユーザーが実機の結果を確認した後に BT_SPEAKER の docs（impl-notes.md・changes.md）へ反映する（Claude Code は反映しない）

## 2. 作業ルール（必ず守る）

- **`C:\workspace\MAKER\BT_SPEAKER` と `_MAKE` 内のファイル・リポジトリは、許可なく変更しない**（編集・git 操作・ファイルの新規作成とも）。参照（読み取り）だけにする。`BT_SPEAKER/tools/` のスクリプトを実行するのは読み取り専用の利用なので可。ただし出力（ログ等）は `atom_tx_test/` 内に出す
- **`atom_test/atom_test/`（既存のスケッチ）は変更しない**。書くのは `atom_tx_test/` の中だけ
- **git の commit・push はしない**（ユーザーが指示したときだけ）
- 推測で決めない。ライブラリ API・FQBN・ピンの挙動・M5Unified のボタンの動作など不確かなものは、ローカルのソース（`%USERPROFILE%\Documents\Arduino\libraries`、`%LOCALAPPDATA%\Arduino15`）・データシートで確認するか、ユーザーに確認する
- **各段階の最後で止まり、実機の結果をユーザーに確認してもらってから次へ進む**
- 開発環境は BT_SPEAKER の `docs/dev-env.md` に準じる。**試験の途中でコア・ライブラリを更新しない**（RX とそろえるため）。使ったバージョンを README に記録する（確認済み: コア m5stack:esp32 3.3.9、M5Unified 0.2.24、ESP32-A2DP 1.8.11）
- 書き込みの前に、ユーザーに伝える: TX は USB 給電のみで M5Dial・Grove は接続しない。書き込み中は RX の電源を切っておく（接続試行が絡むため）

## 3. 読む資料（すべて読み取り専用）

| 資料 | 見るところ |
|---|---|
| `BT_SPEAKER/CLAUDE.md`・`atom_a2dp_instructions.md` | 作業ルール、スケッチ構成、役割判定と起動順序、AtomLite の作業で共通に守ること（4節） |
| `BT_SPEAKER/docs/software.md` 1節・2.3節・2.4節 | デバイス名、A2DP/AVRCP、TX の BT 接続、LED |
| `BT_SPEAKER/docs/hardware.md` 3節 | AtomLite のピン（今回の試験で使うのは G27 LED、G39 ボタンだけ） |
| `BT_SPEAKER/docs/impl-notes.md` 1.1〜1.5節・3.2節・3.5節 | 再接続はライブラリ任せ、RX は TX 切断で再起動、重い処理を入れない、`M5.begin()` のピン操作、LED |
| `BT_SPEAKER/atom_a2dp/`（`config.h`・`atom_a2dp.ino`・`role.*`・`status_led.*`・`dial_link.*`・`rx_audio.cpp`） | コピー元。RX が A2DP Sink をどう初期化しているか（`set_on_connection_state_changed`、`start()`、切断時の再起動） |
| `atom_test/atom_test/tx_audio.*` | TX の枠（中身は TODO のみ） |
| `bt_play_test/README.md` | RX 側の計測ログ（`# pcm …` の行）の見方 |
| `_MAKE`（OneDrive） | Atom・関連モジュール・デバイスの情報。アクセス許可は得ている。フォルダが接続されていない場合は、ユーザーに接続を依頼する |
| `…\libraries\ESP32-A2DP\src\BluetoothA2DPSource.h`・`BluetoothA2DPCommon.h`・`A2DPVolumeControl.h` | Source の API（下の4.2節は確認済みの事項） |

## 4. 仕様（ユーザーが決めたこと）

### 4.1 動作

- 起動すると**常に** `"fermata BT Speaker"`（RX のデバイス名。`BT_DEVICE_NAME`。大文字小文字を区別する前方一致のため、RX と完全に同じ文字列にする）へ接続を試み、接続され次第、ダミーの音声ストリームを送る
- 自分の BT 名（ピア名。RX の `@CONNECTED` に出る名前）は **`"fermata SPDIF"`**
- 接続待ち（未接続）は**青のゆっくり点滅**、接続中は**青の点灯**
- **赤**: S/PDIF（I2S）が動いていない、または S/PDIF のエラーなどを検出したとき、**青の代わりに赤**にする（未接続なら赤の点滅、接続中なら赤の点灯）。今回は S/PDIF を使わないので**実際の検出は実装しない**。代わりに**本体ボタンの短押しで「模擬エラー」を切り替え**、赤の表示を確かめられるようにする（`TX_SIM_ERROR_BUTTON` で外せる）。これは software.md 2.4 の「接続中かつエラー検出中は混色 [OI-20]」と異なる（赤が青の代わり）。**docs は更新せず**、README に「合体時に決める事項」として書く
- **ボタン（G39）を 3 秒長押し**すると、自分（TX）を再起動する（`ESP.restart()`）。3 秒に満たない押下は短押し（上の模擬エラーの切り替え）。長押しで再起動する直前に LED を消す
- **RX から切断されたら再起動する**。条件は「**一度接続した後の切断**」だけ（未接続の間はライブラリの自動再接続に任せる。接続できないまま一定時間で再起動する処理は入れない）。切断を検知してから `TX_RESTART_DELAY_MS`（既定 300ms。RX の `RESTART_DELAY_MS` と同じ）待って再起動する
- TX/RX 切替の GPIO（G25）は**開放のまま**。このスケッチは TX 固定で G25 を読まない（`ROLE` を `ROLE_TX` に固定する。`role.*` は `atom_a2dp` からコピーし、`-DROLE` ではなく `config.h` の既定を `ROLE_TX` にする。atom_a2dp の既定は `ROLE_AUTO` で、ここだけ違う。README に書く）

### 4.2 A2DP Source（ESP32-A2DP 1.8.11 の実ソースで確認済み）

- 使うクラスは `BluetoothA2DPSource`
- 自分の名前: `set_local_name("fermata SPDIF")`（`start()` の前）。`start(name)` の `name` は**接続先**の名前（`strncmp` の前方一致、大文字小文字を区別）
- 再接続: `set_auto_reconnect(true)`（software.md 2.3節、impl-notes.md 1.1節）。初回は名前で inquiry スキャンし、接続したアドレスを NVS に保存して以降は直接接続する。**アプリ側で「切断検出→`start()` の呼び直し」はしない**（impl-notes.md 1.1節。再起動は 4.1節のとおり `ESP.restart()`）
- 音声: `set_data_callback_in_frames(cb)`（`int32_t cb(Frame* data, int32_t len)`。`Frame` は `int16_t channel1`（L）／`channel2`（R）、`A2DPVolumeControl.h`）。44.1kHz・16bit・2ch。**要求されたフレーム数をすべて埋めて `len` を返す**（足りないと 0 を送るのではなく短いデータになる）。コールバックは BT のタスクで呼ばれるので、重い処理・ログ・`delay()` を入れない
- **`set_volume()` は呼ばない**（呼ぶと RX に AVRCP の絶対音量を送り、RX の音量が変わる。呼ばない場合は音量制御が使われず、PCM はそのまま出る）
- `AudioTools.h` は include しない（今回は不要。後の合体で I2S を足すときの `LOG_LEVEL`・`PIN_I2S_*` のマクロ衝突は impl-notes.md 1.5節）
- 接続状態: `set_on_connection_state_changed(cb, obj)`（`void cb(esp_a2d_connection_state_t, void*)`）。コールバックは BT 側のタスク。**状態を volatile 変数に記録するだけ**にし、LED・ログ・再起動は `loop()` で行う（atom_a2dp_instructions.md 4節）。起動直後の接続試行の失敗で `DISCONNECTED` が通知されることがあるため、再起動の条件は「一度 `CONNECTED` になった後」とする
- 接続の確認: `is_connected()`

### 4.3 ダミーの音声（RX の EQ のバンドに合わせた正弦波）

- 周波数: RX のユーザー EQ のバンドに合わせる **100／200／400／800／1600／3200／6300／12500Hz**（`atom_a2dp/dsp.cpp` の `kUserFreqs`、gui.md 7.2節）
- 掃引: 各周波数を **3 秒**ずつ順に鳴らし、最後まで行ったら最初に戻る（`TONE_STEP_MS`）
- 音量: **−20dBFS**（`TONE_LEVEL_DBFS`。ピーク約 3,277）。L=R（`TONE_CHANNEL` で L のみ／R のみにも切り替えられるようにする）
- 周波数の切り替えで位相を飛ばさない（位相の累積は連続。クリックを出さない）。周波数の変更は `loop()` で行い、位相の増分の更新が BT タスクと競合しない形にする（32bit の volatile の書き込み）
- 高調波の歪みを RX 側の EQ の確認に影響させない程度に小さくする（例: 1024 点のテーブル＋線形補間、または `sinf`。コールバックの処理時間を実測して選ぶ）
- モード: `TONE_MODE`（0=掃引、1=固定周波数 `TONE_FIXED_HZ`）。設定はすべて `config.h` のマクロ。`-D` で上書きできる形にする（`LOG_LEVEL` を除く）

### 4.4 ログ

- USB シリアル（115200bps）にだけ、行頭を `#` にして出す（`LOG1`／`LOG2` のマクロ名は atom_a2dp と同じにする。`dial_link.*` をコピーして使えるか確かめる。TX は UART を持たないので `dialBegin(false)` で Serial1 を開かないこと。使いにくければ最小の代替を作り、README に書く）
- BT のコールバックの中では出さない。`loop()` で、状態が変わったときと、1 秒ごと（`LOG_LEVEL` 2）に次を出す: 接続状態、現在の周波数、1 秒あたりに作ったフレーム数（約 44,100 のはず）、空きヒープと最小値
- ログが多いと音切れの原因になる。重い処理は入れない

## 5. ファイル構成（atom_a2dp にそろえる）

| ファイル | 内容 | コピー元 |
|---|---|---|
| `atom_tx_test.ino` | 役割の判定 → `M5.begin()` → 共通部 → `txSetup()`／`txLoop()` | `atom_a2dp/atom_a2dp.ino`（TX の分岐だけ） |
| `config.h` | 節を「共通」「RX（今回は書かない。コメントのみ）」「TX」に分ける。共通はコピー元と同じ書き方 | `atom_a2dp/config.h` |
| `role.*` | 役割の判定（`ROLE` を `ROLE_TX` 固定） | `atom_a2dp/role.*` |
| `status_led.*` | 本体 LED。**`LED_RED_BLINK`（赤の点滅）を足す**（接続待ち＋エラー用） | `atom_a2dp/status_led.*`（`LED_RED` あり） |
| `dial_link.*`（または最小の代替） | ログ（`LOG1`／`LOG2`） | `atom_a2dp/dial_link.*` |
| `tx_main.*` | TX の入口（`txSetup()`／`txLoop()`）: 状態の管理、LED、ボタン、切断時の再起動 | 新規（`rx_main.*` の形を参考にする） |
| `tx_audio.*` | A2DP Source の初期化、ダミー信号の生成、接続状態の記録 | 新規（`rx_audio.*` の形を参考にする） |
| `README.md` | ビルド・書き込み手順、使ったバージョン、各段階の結果、atom_a2dp との差分、合体時に決める事項 | 新規 |
| `.gitignore` | ログ（`logs/`）などを除外 | 新規 |

- コピーしたファイルは、先頭のコメントにコピー元（リポジトリ・パス・コミット）と変えた点を書く。**変更は最小限**にする（合体のときの差分を小さくするため）
- 役割に固有の処理を `.ino`・共通のファイルに書かない（atom_a2dp_instructions.md 1節）
- `M5.begin()` の前後の順序、`cfg.internal_imu = false; cfg.internal_rtc = false;`、`ledBegin()` は atom_a2dp.ino のとおり

## 6. 作業の段階

各段階の最後で止まり、結果をユーザーに報告して確認を取る。

### 段階0: 準備

- 3節の資料を読む（`_MAKE` が読めなければユーザーに依頼）。`arduino-cli version`・`core list`・`lib list` で、環境が dev-env.md の確認済みバージョンと同じか確かめる（違えば報告。更新はしない）
- `atom_tx_test/` にファイルを作る（コンパイルできる最小の状態まで）。ユーザーに確認すること: RX（`atom_a2dp`）の COM ポート、M5Dial を接続したままにするか（TX の試験には不要）

### 段階1: ビルド

- `arduino-cli compile --fqbn m5stack:esp32:m5stack_atom:UploadSpeed=115200 atom_tx_test`（`atom_test` フォルダで実行）
- 警告なしでビルドできることと、フラッシュ・グローバル変数の使用量（RX の A1: 約 48KB、フラッシュは A2DP で約 1.26MB が目安）を README に記録する

### 段階2: TX 単体（RX の電源は切る）

- ユーザーに「RX の電源は切る、TX は USB のみ」を確認してから `arduino-cli upload -p COM7 …`。**最初の 1 回は NVS（前の試験で保存された接続先アドレス）が残っている可能性がある**。名前でのスキャンの経路を確かめたいので、ユーザーに「フラッシュ全消去（`esptool erase_flash`）をするか」を確認する（しない場合は、その旨を README に書く）
- 確認: 青のゆっくり点滅。ログに接続試行・1 秒ごとの状態。ボタンの短押しで赤の点滅 ⇔ 青の点滅、3 秒長押しでリブート

### 段階3: RX につなぐ（A2DP の伝送）

- RX は `atom_a2dp`（RX）。**スマホ等が RX に接続していないこと**（RX は 1 台しか接続できない）。RX のログは `BT_SPEAKER/tools/serial_log.ps1`（DTR・RTS 無効。出力は `atom_tx_test/logs/` へ）で取る
- 確認:
  - RX の電源を入れる → TX が接続し、LED が青の点灯になる。RX のログに接続とピア名 `fermata SPDIF`
  - RX の `# pcm` の行で、約 176,128 B/s、間隔の最大・50ms 超の回数、リングバッファが空にならないこと
  - **耳で**: 8 つの周波数が順に聞こえる。途切れ・ノイズ・クリックがない（5 分）
  - 模擬エラーのボタンで、接続中は赤の点灯、解除で青の点灯
  - TX のログ: 1 秒あたりのフレーム数が約 44,100、空きヒープの最小値

### 段階4: 切断・再接続・起動順序

- RX を先に起動 → TX を起動、TX を先に起動 → RX を起動、どちらでも接続する
- RX を再起動（電源断／USB のリセット）→ TX が切断を検知して約 300ms 後に再起動 → 青の点滅 → RX の復帰後に再接続
- RX の `!dial KILL`（USB の `!` の行）で切断 → TX の再起動と再接続（`DEBUG_CMD=1` のとき。使い方は `atom_a2dp/README.md`）
- TX 単体のリセット（ボタンの長押し）→ 再起動 → **保存されたアドレスへ直接接続**する経路で再接続する。名前でのスキャン（初回）と保存アドレス（2 回目以降）の接続にかかった時間をそれぞれ記録する

### 段階5: 連続動作

- 30 分の連続送信。切断・再起動の有無、空きヒープの最小値の推移（impl-notes.md 1.6節: 約 5KB を切ると BT が止まる前例）、音の異常

### 段階6: 結果のまとめ（合体の準備）

README に次を書く:
- 各段階の結果（数値付き）
- `atom_a2dp` との差分（`ROLE` の既定、`LED_RED_BLINK` の追加、`tx_main.*`・`tx_audio.*` の形）
- 合体時に決める事項（赤の扱いと software.md 2.4節の OI-20 との違い、模擬エラーのボタンを残すか、`TX_RESTART_DELAY_MS`、ダミー信号を S/PDIF（I2S）の入力に置き換える位置）
- 分かったこと・落とし穴（BT_SPEAKER の impl-notes.md に反映する候補。**反映はユーザーの確認後**）

## 7. 完了の条件

段階 3〜5 の合格条件を、実機でユーザーが確認できたこと。S/PDIF（I2S）は、BT 関係の確認が取れるまで接続しない（本スケッチの範囲外）。
