# RX への変更のリクエスト: TX の S/PDIF の状態を曲名として表示する

作成: 2026-10-03（atom_tx_test の作業から。Claude Code）
対象: `BT_SPEAKER/atom_a2dp/rx_audio.cpp`（c1d9300）。M5Dial（dial_gui）は変えない見込み
状態: **実装・確認済み**（2026-10-03。ユーザーの指示で、atom_tx_test の作業の中で RX を変更して書き込み、TX とともに確かめた。BT_SPEAKER へのコミットはしていない）

## 0. 結果（2026-10-03）

- 変更: `atom_a2dp/rx_audio.cpp`（シンクを `TgHook<…>` で包み、Target の接続時に PLAY・STOP を受け付ける設定にし、受けた状態をピア名が TX のときだけ曲名にする）、`atom_a2dp/config.h`（`BT_TX_PEER_NAME`）。3節の案のとおり
- ビルド: `USE_QUEUED_SINK` 1・0 とも警告なし。グローバル変数 59,704 → 59,728 B
- TX のパススルーの応答: NOT_IMPL → **ACCEPT（9）**。RX のログ `avrcp tg: accept PLAY ok STOP ok (set filter 0)`
- M5Dial の曲名: `S/PDIF PLAYING` ⇔ `S/PDIF ERROR`（TX の模擬エラーの短押しで切り替わる。`STATUS` の応答でも返る）。接続から約1秒で出る
- PC（`YOKOGAWA-NOTE`）・iPhone との接続: 今までどおりのメタ情報（曲名に S/PDIF の状態は入らない）
- SLEEP → ウェイク → TX と再接続: 再接続の後に状態が出る
- RX の空きヒープ: 接続中 約 21KB（最小 14.8KB）
- ログ: `logs/avrcp_rx.txt`（RX）、`logs/avrcp_psth.txt`（TX）。ユーザーの確認: 模擬エラーの表示、スマホ・PC との接続、SLEEP に問題なし

## 1. やりたいこと（ユーザーの判断、2026-10-03）

TX（`fermata SPDIF`）と接続している間、M5Dial の曲名の欄に S/PDIF の状態を出す。

| TX の状態 | 曲名 |
|---|---|
| 正常 | `S/PDIF PLAYING` |
| エラーを検出中（RERR、I2S 受信タイムアウト。software.md 2.2節） | `S/PDIF ERROR` |

スマホ・PC と接続しているときは、今までどおり相手のメタ情報を出す。

## 2. 伝え方（atom_tx_test で調べたこと）

ESP-IDF（Bluedroid）の AVRCP Target では、TX から曲名も再生状態も送れない:

- 曲名: RX が送る `GetElementAttributes` に答える API・イベントが、Target に無い（`esp_avrc_api.h`）
- 再生状態の通知（`PLAY_STATUS_CHANGE`）: Target が許す通知は音量の変化だけ。TX で `esp_avrc_tg_get_rn_evt_cap(ESP_AVRC_RN_CAP_ALLOWED_EVT)` が `0x2000`（bit13 = `VOLUME_CHANGE`）だった（実機）。RX は登録しに来ない

そこで、**TX の AVRCP Controller から RX の Target へ、パススルーのコマンドを送る**:

| TX の状態 | パススルー（押す → 離すを続けて送る） |
|---|---|
| 正常 | `ESP_AVRC_PT_CMD_PLAY`（0x44） |
| エラー | `ESP_AVRC_PT_CMD_STOP`（0x45） |

- 送るとき: AVRCP の Controller が RX とつながってから 1 秒後（`TX_PSTH_DELAY_MS`。RX がピア名を受け取るのを待つ）と、エラーの有無が変わったとき
- 実装済み（`atom_tx_test/tx_audio.cpp` の `avrcpLoop()`、`TX_AVRCP_STATUS=1`）

**現状の RX の応答は NOT_IMPL（8）**（2026-10-03、TX のログ `logs/avrcp_psth.txt`）:

```
avrcp: passthrough 0x44 (PLAYING)
avrcp: passthrough response key 0x44 pressed: NOT_IMPL (8)
avrcp: passthrough response key 0x44 released: NOT_IMPL (8)
```

ESP32-A2DP 1.8.11 の `BluetoothA2DPSink` は Target を初期化するが、受け付けるパススルー（`esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, …)`）を設定していない（`BluetoothA2DPSink.cpp` の `av_hdl_avrc_tg_evt`。`BluetoothA2DPSource` は Target の接続時に設定している）。そのため ESP-IDF が NOT_IMPL を返し、アプリにはイベントが届かないと考えられる。

## 3. RX の変更の内容（案）

### 3.1 PLAY／STOP を受け付け、受けたものを記録する

`rx_audio.cpp` のシンク（`USE_QUEUED_SINK=1` は `MonSink`、0 は `BluetoothA2DPSink`）の派生クラスで `av_hdl_avrc_tg_evt()`（`BluetoothA2DPSink.h` で protected の virtual）を上書きする。BT 側のタスクで呼ばれるので、記録だけにする（atom_a2dp_instructions.md 4節）。

```cpp
// Target の接続時: PLAY・STOP を受け付ける（ライブラリは設定しないので、NOT_IMPL が返る）
void av_hdl_avrc_tg_evt(uint16_t event, void* p_param) override {
  BaseSink::av_hdl_avrc_tg_evt(event, p_param);
  auto* rc = static_cast<esp_avrc_tg_cb_param_t*>(p_param);
  if (event == ESP_AVRC_TG_CONNECTION_STATE_EVT && rc->conn_stat.connected) {
    esp_avrc_psth_bit_mask_t set = {};
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &set, ESP_AVRC_PT_CMD_PLAY);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &set, ESP_AVRC_PT_CMD_STOP);
    esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &set);
  } else if (event == ESP_AVRC_TG_PASSTHROUGH_CMD_EVT && rc->psth_cmd.key_state == ESP_AVRC_PT_CMD_STATE_PRESSED) {
    // PLAY → 正常、STOP → エラー。ほかのキーは無視
    if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_PLAY) s_txState = TX_PLAYING;   // atomic
    if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_STOP) s_txState = TX_ERROR;
  }
}
```

- 受け付ける設定は、ESP-IDF の許す範囲（`ESP_AVRC_PSTH_FILTER_ALLOWED_CMD`）の中でなければならない。PLAY・STOP が許されているかは、実装のときに `esp_avrc_tg_get_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_ALLOWED_CMD, …)` で確かめる
- `s_txState` は接続のたび（`onConnectionState` の CONNECTED）に「未受信」に戻す

### 3.2 ピア名が TX のときだけ、曲名として送る

`rxAudioLoop()` で、ピア名が `"fermata SPDIF"`（TX の `BT_TX_LOCAL_NAME`。前方一致）で、`s_txState` を受けていれば、曲名（`META_TITLE`）の `s_meta` に `"S/PDIF PLAYING"`／`"S/PDIF ERROR"` を入れて dirty にする（変わったときだけ）。今の `sendMeta()` がそのまま `@META{"1","…"}` で送り、`STATUS` の応答（`sendMeta(false)`）でも返る。

- ピア名は接続の後に少し遅れて届く。パススルーが先に届いても、`s_txState` を覚えておき、ピア名が TX と分かった時点で反映する
- スマホ・PC と接続しているとき（ピア名が TX でない）は、パススルーが来ても曲名に入れない（スマホ・PC は通常 RX へパススルーを送らないが、念のため）
- TX の名前は RX の `config.h` に置く（例 `BT_TX_PEER_NAME "fermata SPDIF"`。atom_a2dp に TX を合体するときは、TX の `BT_TX_LOCAL_NAME` と同じ定義にまとめる）

### 3.3 確かめること

- TX の短押し（模擬エラー）で、M5Dial の曲名が `S/PDIF PLAYING` ⇔ `S/PDIF ERROR` に変わる。TX のログでパススルーの応答が `ACCEPT（9）` になる
- RX・TX の再起動、接続の直後に、曲名が出る（`STATUS` の応答でも）
- スマホ・PC と接続したときは、今までどおりのメタ情報（パススルーの設定で変わらない）
- 再生（RX の `# pcm`・`# dsp` の行）に影響がない

## 4. docs への影響（RX の変更が決まったら、変更の案を提示して許可を得る）

- software.md 1節: 「TX: AVRCP は扱わない」（2026-10-03 に変えたばかり）を、「TX: AVRCP の Controller から、S/PDIF の状態をパススルー（PLAY／STOP）で RX に送る」に変える
- software.md 3.1節: 「メタ情報が無い場合は空として送る（TX接続時は常にこのケース）」を、TX と接続しているときは S/PDIF の状態を曲名として送る、に変える
- changes.md: 判断の記録（曲名・再生状態の通知が ESP-IDF の Target で使えないこと、パススルーを選んだ理由、ピア名で区別すること）
- impl-notes.md: ESP-IDF の AVRCP Target の制約（GetElementAttributes に答えられない、通知は VOLUME_CHANGE だけ）、Sink はパススルーを受け付ける設定をしない
