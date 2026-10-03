// tx_audio.cpp — TX（A2DP Source）。ダミーの音声（正弦波）を送る（atom_tx_test_instructions.md 4.2節・4.3節）
// 形は BT_SPEAKER/atom_a2dp/rx_audio.cpp（c1d9300）を参考にした（コールバックは記録だけ、ログは loop() 側）
//
// ESP32-A2DP 1.8.11 のソースで確認:
// - set_local_name() は自分の名前（start() の前に呼ぶ）。start(name) の name は接続先で、inquiry の結果の名前と
//   strncmp(相手の名前, name, strlen(name)) で比べる（前方一致、大文字小文字を区別。BluetoothA2DPSource.cpp）
// - set_auto_reconnect(true): start() で NVS に保存された前回の接続先を読み、あれば直接 connect_to() する。
//   無ければ名前で inquiry スキャンし、見つけたアドレスを NVS に保存する。直接接続の再試行が尽きるとスキャンに戻る
// - set_data_callback_in_frames(cb): 1回に len フレームを要求される。返した数×4 バイトが送られる
//   （get_audio_data。足りないと短いデータになる）。その後 volume_control()->update_audio_data() が呼ばれるが、
//   set_volume() を呼ばなければ（is_volume_used=false）PCM はそのまま（A2DPVolumeControl.h）
// - 接続状態のコールバックは BT 側のタスクで呼ばれる。A2DP の CONNECTION_STATE_EVT のほか、ACL の切断
//   （ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT）でも DISCONNECTED が通知される（起動直後の接続試行の失敗など）
#include "BluetoothA2DPSource.h"
#include <Arduino.h>
#include <atomic>
#include <math.h>
#include <esp_timer.h>
#include "tx_audio.h"
#include "tx_spdif.h"
#include "config.h"
#include "dial_link.h"

// AVRCP で S/PDIF の状態を RX に知らせる（TX_AVRCP_STATUS）:
// - ESP-IDF の AVRCP Target は、曲名などの要求（GetElementAttributes）に答えられず（API が無い。esp_avrc_api.h）、
//   通知も音量の変化（VOLUME_CHANGE）しか許さない（esp_avrc_tg_get_rn_evt_cap(ALLOWED) が 0x2000。実機で確認）。
//   そのため再生状態の通知（PLAY_STATUS_CHANGE）も使えない
// - 代わりに、Controller（ライブラリが常に初期化する）からパススルーのコマンドを送る: 正常は TX_PSTH_KEY_OK（PLAY）、
//   エラーは TX_PSTH_KEY_ERR（STOP）。押す（PRESSED）と離す（RELEASED）を続けて送る。RX の Target が受けて、
//   ピア名が "fermata SPDIF" のときだけ曲名（"S/PDIF PLAYING"／"S/PDIF ERROR"）として M5Dial に送る（RX の変更が要る）
// - Controller のイベント（接続、パススルーの応答）は bt_av_hdl_avrc_ct_evt（BT 側のタスク）で届くので、派生クラスで
//   受けて記録する。送るのは loop() から
static constexpr uint32_t kSampleRate = 44100;
// RX のユーザー EQ のバンド（atom_a2dp/dsp.cpp の kUserFreqs、gui.md 7.2節）
static constexpr uint16_t kSweepFreqs[] = {100, 200, 400, 800, 1600, 3200, 6300, 12500};
static constexpr int kSweepCount = sizeof(kSweepFreqs) / sizeof(kSweepFreqs[0]);

// ---- AVRCP Controller のイベント（BT 側のタスク → loop()）----
static std::atomic<bool> s_ctConn{false};       // Controller が RX の Target とつながっている
static std::atomic<uint32_t> s_ctConnSeq{0};    // つながった回数（loop() が新しい接続を見分ける）
static std::atomic<uint32_t> s_psthRspSeq{0};   // パススルーの応答を受けた回数
static std::atomic<uint32_t> s_psthRsp{0};      // 最後の応答: key_code << 16 | key_state << 8 | rsp_code

class TxSource : public BluetoothA2DPSource {
 protected:
  void bt_av_hdl_avrc_ct_evt(uint16_t event, void* p_param) override {
    BluetoothA2DPSource::bt_av_hdl_avrc_ct_evt(event, p_param);
    auto* rc = static_cast<esp_avrc_ct_cb_param_t*>(p_param);
    if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT) {
      s_ctConn.store(rc->conn_stat.connected, std::memory_order_relaxed);
      if (rc->conn_stat.connected) s_ctConnSeq.fetch_add(1, std::memory_order_relaxed);
    } else if (event == ESP_AVRC_CT_PASSTHROUGH_RSP_EVT) {
      s_psthRsp.store((uint32_t)rc->psth_rsp.key_code << 16 | (uint32_t)rc->psth_rsp.key_state << 8 | rc->psth_rsp.rsp_code,
                      std::memory_order_relaxed);
      s_psthRspSeq.fetch_add(1, std::memory_order_relaxed);
    }
  }
};

// rx_audio と同じく setup の中で作る
static TxSource* s_srcp = nullptr;
#define s_src (*s_srcp)
static uint32_t s_startMs = 0;
static volatile bool s_error = false;  // エラー中（S/PDIF または模擬エラー）。loop() が書き、BT タスクが読む

// ---- BT タスクと loop() で共有する状態 ----
static volatile int s_connState = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
static volatile bool s_wasConnected = false;    // 一度 CONNECTED になった
static std::atomic<int> s_audioState{-1};       // esp_a2d_audio_state_t（-1: 未受信）
static std::atomic<bool> s_evAudioState{false};
static volatile uint32_t s_phaseInc = 0;        // 位相の増分（loop() が書き、BT タスクが読む。32bit の書き込み）

// ---- 計測（BT タスクで記録し、loop() でログに出す）----
static std::atomic<uint32_t> s_frames{0};       // 作ったフレーム数（前回のログ以降）
static std::atomic<uint32_t> s_calls{0};        // コールバックの回数
static std::atomic<uint32_t> s_lenMax{0};       // 1回に要求された最大フレーム数
static std::atomic<uint32_t> s_cbUsMax{0};      // コールバックの最大処理時間（µs）

// ---- 正弦波 ----
static uint32_t s_phase = 0;  // onFrames() だけが使う（32bit で1周期）
static uint16_t s_toneHz = 0;
static int s_toneIdx = 0;
static uint32_t s_toneT0 = 0;
#if TONE_USE_SINF
static float s_ampF = 0;
#else
static constexpr int kTabBits = 10;  // 1024 点
static int16_t s_sinTab[(1 << kTabBits) + 1];  // 最後の1点は補間用（= 先頭）
static int32_t s_amp = 0;   // Q15
#endif

static void atomicMax(std::atomic<uint32_t>& a, uint32_t v) {
  uint32_t prev = a.load(std::memory_order_relaxed);
  while (v > prev && !a.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {
  }
}

// BT タスクで呼ばれる。要求されたフレーム数をすべて埋めて返す。ログ・delay() を入れない
[[maybe_unused]] static void makeTone(Frame* data, int32_t len);  // TX_AUDIO_SOURCE=0 だけで使う

static int32_t onFrames(Frame* data, int32_t len) {
  int64_t t0 = esp_timer_get_time();
#if TX_AUDIO_SOURCE == 0
  makeTone(data, len);
#else
  spdifRead(data, len);  // エラー中・準備中は無音
#endif
#if TX_MUTE_ON_ERROR
  if (s_error) memset(data, 0, sizeof(Frame) * len);
#endif
  s_frames.fetch_add(len, std::memory_order_relaxed);
  s_calls.fetch_add(1, std::memory_order_relaxed);
  atomicMax(s_lenMax, (uint32_t)len);
  atomicMax(s_cbUsMax, (uint32_t)(esp_timer_get_time() - t0));
  return len;
}

static void makeTone(Frame* data, int32_t len) {
  uint32_t inc = s_phaseInc;
  uint32_t ph = s_phase;
  for (int32_t i = 0; i < len; i++) {
#if TONE_USE_SINF
    int16_t s = (int16_t)lrintf(sinf((float)ph * (float)(2.0 * M_PI / 4294967296.0)) * s_ampF);
#else
    uint32_t idx = ph >> (32 - kTabBits);
    int32_t frac = (ph >> (32 - kTabBits - 16)) & 0xFFFF;
    int32_t a = s_sinTab[idx];
    int32_t b = s_sinTab[idx + 1];
    int32_t v = a + (((b - a) * frac) >> 16);
    int16_t s = (int16_t)((v * s_amp) >> 15);
#endif
    data[i].channel1 = (TONE_CHANNEL == 2) ? 0 : s;  // L
    data[i].channel2 = (TONE_CHANNEL == 1) ? 0 : s;  // R
    ph += inc;
  }
  s_phase = ph;
}

static void onConnectionState(esp_a2d_connection_state_t state, void*) {
  s_connState = state;
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) s_wasConnected = true;
}

static void onAudioState(esp_a2d_audio_state_t state, void*) {
  s_audioState.store(state, std::memory_order_relaxed);
  s_evAudioState.store(true, std::memory_order_relaxed);
}

static const char* audioStateName(int state) {
  switch (state) {
    case ESP_A2D_AUDIO_STATE_SUSPEND: return "SUSPEND";
    case ESP_A2D_AUDIO_STATE_STARTED: return "STARTED";
    case -1: return "-";
    default: return "?";
  }
}

// 周波数を変える（loop() から。位相は連続のまま増分だけ替える）
static void setTone(uint16_t hz) {
  s_toneHz = hz;
  s_phaseInc = (uint32_t)((double)hz * 4294967296.0 / kSampleRate + 0.5);
  LOG1("tone %u Hz", hz);
}

// ---- 公開関数 ----

const char* txAudioStateName(int state) {
  switch (state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED: return "DISCONNECTED";
    case ESP_A2D_CONNECTION_STATE_CONNECTING: return "CONNECTING";
    case ESP_A2D_CONNECTION_STATE_CONNECTED: return "CONNECTED";
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return "DISCONNECTING";
    default: return "?";
  }
}

int txAudioConnState() { return s_connState; }
bool txAudioWasConnected() { return s_wasConnected; }
uint32_t txAudioStartMs() { return s_startMs; }
bool txAudioBtStarted() { return s_startMs != 0; }

void txAudioPeerAddress(char* buf, int size) {
  const uint8_t* a = *s_src.get_last_peer_address();
  snprintf(buf, size, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

void txAudioRestartTone() {
  s_toneIdx = 0;
  s_toneT0 = millis();
  setTone(TONE_MODE == 0 ? kSweepFreqs[0] : TONE_FIXED_HZ);
}

void txAudioSetup() {
  float amp = 32767.0f * powf(10.0f, (float)TONE_LEVEL_DBFS / 20.0f);
#if TONE_USE_SINF
  s_ampF = amp;
#else
  for (int i = 0; i <= (1 << kTabBits); i++) {
    s_sinTab[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * i / (1 << kTabBits)));
  }
  s_amp = (int32_t)lrintf(amp);
#endif
  txAudioRestartTone();

  spdifBegin();  // TX_AUDIO_SOURCE=1/2: 受信タスクを始める（BT より先に、リングバッファを貯め始める）

  s_srcp = new TxSource();
  s_src.set_local_name(BT_TX_LOCAL_NAME);
  s_src.set_auto_reconnect(true);  // software.md 2.3節、impl-notes 1.1節。切断後の start() の呼び直しはしない
  s_src.set_on_connection_state_changed(onConnectionState);
  s_src.set_on_audio_state_changed(onAudioState);
  s_src.set_data_callback_in_frames(onFrames);
  // set_volume() は呼ばない（呼ぶと RX に AVRCP の絶対音量を送り、RX の音量が変わる）
}

// A2DP を始める（TX_BT_NEED_SPDIF=1 なら、S/PDIF が正常になってから tx_main が呼ぶ。1回だけ）
void txAudioStartBt() {
  if (s_startMs != 0) return;
  s_startMs = millis() | 1;
  s_src.start(BT_DEVICE_NAME);

  // start() の中で NVS から読んだ前回の接続先（全0なら無し → 名前でスキャン）
  char addr[18];
  txAudioPeerAddress(addr, sizeof(addr));
  LOG1("A2DP source started: local \"%s\" -> \"%s\", saved peer %s, source %d, tone %s %d dBFS ch %d (%s), avrcp status %d",
       BT_TX_LOCAL_NAME, BT_DEVICE_NAME, strcmp(addr, "00:00:00:00:00:00") ? addr : "none (scan by name)",
       TX_AUDIO_SOURCE, TONE_MODE == 0 ? "sweep" : "fixed", TONE_LEVEL_DBFS, TONE_CHANNEL,
       TONE_USE_SINF ? "sinf" : "table", TX_AVRCP_STATUS);
}

void txAudioSetError(bool error) { s_error = error; }
bool txAudioSpdifError() { return spdifError(); }
const char* txAudioSpdifReason() { return spdifErrorReason(); }

#if TX_AVRCP_STATUS
static const char* rspName(int rsp) {
  switch (rsp) {
    case ESP_AVRC_RSP_ACCEPT: return "ACCEPT";
    case ESP_AVRC_RSP_REJECT: return "REJECT";
    case ESP_AVRC_RSP_NOT_IMPL: return "NOT_IMPL";
    default: return "?";
  }
}

// パススルーで状態を送る: Controller が RX の Target とつながってから TX_PSTH_DELAY_MS 後と、エラーの有無が
// 変わったときに、押す・離すを続けて送る。送るのは loop() からだけ
static void avrcpLoop() {
  static uint32_t connSeen = 0;
  static uint32_t connMs = 0;
  static bool pending = false;  // 接続の後、送るのを待っている
  static bool sentError = false;
  static bool sentOnce = false;
  static uint8_t tl = 0;
  uint32_t now = millis();
  bool err = s_error;
  uint32_t seq = s_ctConnSeq.load(std::memory_order_relaxed);
  if (seq != connSeen) {
    connSeen = seq;
    connMs = now;
    pending = true;
    sentOnce = false;
    LOG1("avrcp: controller connected");
  }
  bool conn = s_ctConn.load(std::memory_order_relaxed);
  bool send = false;
  if (conn && pending && now - connMs >= TX_PSTH_DELAY_MS) {
    pending = false;
    send = true;
  } else if (conn && sentOnce && err != sentError) {
    send = true;
  }
  if (send) {
    uint8_t key = err ? TX_PSTH_KEY_ERR : TX_PSTH_KEY_OK;
    esp_err_t e1 = esp_avrc_ct_send_passthrough_cmd(tl, key, ESP_AVRC_PT_CMD_STATE_PRESSED);
    tl = (tl + 1) & 0x0F;
    esp_err_t e2 = esp_avrc_ct_send_passthrough_cmd(tl, key, ESP_AVRC_PT_CMD_STATE_RELEASED);
    tl = (tl + 1) & 0x0F;
    sentError = err;
    sentOnce = true;
    LOG1("avrcp: passthrough 0x%02x (%s)%s", key, err ? "ERROR" : "PLAYING",
         (e1 == ESP_OK && e2 == ESP_OK) ? "" : " (send failed)");
  }
  // RX からの応答（受け付けたか）
  static uint32_t rspSeen = 0;
  uint32_t rseq = s_psthRspSeq.load(std::memory_order_relaxed);
  if (rseq != rspSeen) {
    rspSeen = rseq;
    uint32_t r = s_psthRsp.load(std::memory_order_relaxed);
    int rsp = r & 0xFF;
    LOG1("avrcp: passthrough response key 0x%02x %s: %s (%d)", (unsigned)(r >> 16), ((r >> 8) & 0xFF) ? "released" : "pressed",
         rspName(rsp), rsp);
  }
}
#endif

void txAudioLoop() {
#if TONE_MODE == 0
  // 掃引: TONE_STEP_MS ごとに次の周波数へ
  uint32_t nowTone = millis();
  if (nowTone - s_toneT0 >= TONE_STEP_MS) {
    s_toneT0 += TONE_STEP_MS;
    s_toneIdx = (s_toneIdx + 1) % kSweepCount;
    setTone(kSweepFreqs[s_toneIdx]);
  }
#endif

  if (s_evAudioState.exchange(false, std::memory_order_relaxed)) {
    LOG1("audio state: %s", audioStateName(s_audioState.load(std::memory_order_relaxed)));
  }
#if TX_AVRCP_STATUS
  avrcpLoop();
#endif

#if LOG_LEVEL >= 2
  // 1秒ごと: 接続状態、周波数、1秒あたりのフレーム数（約 44,100）、コールバックの回数・最大の要求フレーム数・最大処理時間、空きヒープ
  static uint32_t lastLog = 0;
  uint32_t now = millis();
  if (now - lastLog >= 1000) {
    uint32_t frames = s_frames.exchange(0, std::memory_order_relaxed);
    uint32_t calls = s_calls.exchange(0, std::memory_order_relaxed);
    uint32_t lenMax = s_lenMax.exchange(0, std::memory_order_relaxed);
    uint32_t cbUsMax = s_cbUsMax.exchange(0, std::memory_order_relaxed);
    LOG2("%s, %s, tone %u Hz, frames %lu/s (calls %lu, max %lu frames, cb max %luus), heap %lu min %lu",
         s_startMs ? txAudioStateName(s_connState) : "BT NOT STARTED", audioStateName(s_audioState.load(std::memory_order_relaxed)), s_toneHz,
         (unsigned long)(frames * 1000ULL / (now - lastLog)), (unsigned long)calls, (unsigned long)lenMax,
         (unsigned long)cbUsMax, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
#if TX_AUDIO_SOURCE != 0
    // 入力・変換: 入力／変換後／BT に渡したフレーム数、足りなかった回数、あふれ、リングバッファの量、比率の調整、変換の時間
    SpdifStats s;
    spdifTakeStats(&s);
    LOG2("spdif %s (rate %lu Hz): in %lu out %lu read %lu /s, under %lu over %lu, ring %lu-%lu, adj %ld ppm, proc max %luus, "
         "restarts %lu",
         spdifErrorReason(), (unsigned long)s.rateHz, (unsigned long)s.inFrames, (unsigned long)s.outFrames, (unsigned long)s.readFrames,
         (unsigned long)s.underruns, (unsigned long)s.overruns, (unsigned long)s.ringMin, (unsigned long)s.ringMax,
         (long)s.adjPpm, (unsigned long)s.procUsMax, (unsigned long)s.restarts);
#endif
    lastLog = now;
  }
#endif
}
