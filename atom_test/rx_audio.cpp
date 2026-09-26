// rx_audio.cpp — RX（A2DP Sink）
//
// Bluetooth のコールバックは BT タスクで呼ばれる。コールバックでは状態を記録するだけにし、
// シリアル送信・LED・再起動は loop() 側（rxLoop）で行う（impl-notes 1.4: 音声タスクに重い処理を入れない）
#include <Arduino.h>
#include <atomic>
#include "BluetoothA2DPSink.h"
#include "rx_audio.h"
#include "config.h"
#include "dial_link.h"
#include "status_led.h"

static BluetoothA2DPSink s_sink;

// ---- BT タスクと loop() で共有する状態 ----
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_connected = false;       // loop() 側で確定した接続状態
static bool s_evConnected = false;     // BT タスク → loop(): 接続した
static bool s_evDisconnected = false;  // BT タスク → loop(): 切断した
static bool s_evPeerName = false;      // BT タスク → loop(): ピア名が届いた
static char s_mac[18] = "";
static char s_peerName[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = "";
static std::atomic<uint32_t> s_pcmBytes{0};

// ---- AVRCP メタ情報 ----
// @META{"<id>","<文字列>"} の id は AVRCP の属性ID（esp_avrc_md_attr_mask_t のビット値）を10進で送る
enum MetaId : uint8_t {
  META_TITLE      = ESP_AVRC_MD_ATTR_TITLE,       // 1  曲名
  META_ARTIST     = ESP_AVRC_MD_ATTR_ARTIST,      // 2  アーティスト
  META_ALBUM      = ESP_AVRC_MD_ATTR_ALBUM,       // 4  アルバム
  META_TRACK_NUM  = ESP_AVRC_MD_ATTR_TRACK_NUM,   // 8  トラック番号
  META_NUM_TRACKS = ESP_AVRC_MD_ATTR_NUM_TRACKS,  // 16 トラック数
  META_GENRE      = ESP_AVRC_MD_ATTR_GENRE,       // 32 ジャンル
};
static constexpr uint8_t kMetaIds[] = {META_TITLE, META_ARTIST, META_ALBUM, META_TRACK_NUM, META_NUM_TRACKS, META_GENRE};
static constexpr int kMetaCount = sizeof(kMetaIds) / sizeof(kMetaIds[0]);
static constexpr int kMetaMask = META_TITLE | META_ARTIST | META_ALBUM | META_TRACK_NUM | META_NUM_TRACKS | META_GENRE;

struct MetaSlot {
  bool valid;  // この接続で受信済み
  bool dirty;  // 未送信
  char text[META_TEXT_MAX];
};
static MetaSlot s_meta[kMetaCount];  // s_mux で保護

static int metaIndex(uint8_t id) {
  for (int i = 0; i < kMetaCount; i++) {
    if (kMetaIds[i] == id) return i;
  }
  return -1;
}

// UTF-8 の文字の途中で切らないようにコピーする
static void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) n--;  // 継続バイトなら戻る
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void clearMeta() {  // s_mux 内で呼ぶ
  for (auto& m : s_meta) {
    m.valid = m.dirty = false;
    m.text[0] = '\0';
  }
}

// dirty（onlyDirty=true）または valid な項目を送る
static void sendMeta(bool onlyDirty) {
  for (int i = 0; i < kMetaCount; i++) {
    char text[META_TEXT_MAX];
    bool send;
    portENTER_CRITICAL(&s_mux);
    send = onlyDirty ? s_meta[i].dirty : s_meta[i].valid;
    if (send) {
      memcpy(text, s_meta[i].text, sizeof(text));
      s_meta[i].dirty = false;
    }
    portEXIT_CRITICAL(&s_mux);
    if (send) {
      char id[4];
      snprintf(id, sizeof(id), "%u", kMetaIds[i]);
      dialSendMeta(id, text);
    }
  }
}

// ---- 音声出力 ----

// 受信した PCM（44.1kHz / 16bit / 2ch、ボリューム適用後）。BT タスクで呼ばれる
static void onPcm(const uint8_t* data, uint32_t len) {
  (void)data;
  s_pcmBytes.fetch_add(len, std::memory_order_relaxed);
  // DAC 接続後はここ（またはライブラリの I2S 出力）で PCM5122 へ出す。rxOutputBegin() 参照
}

// I2S 出力の初期化（DAC 未接続のため今は何もしない）。
// 有効にするときは、s_sink.start() の前に audio-tools の I2SStream を
// BCK=G22（PIN_I2S_BCK）/ WS=G19（PIN_I2S_WS）/ DATA=G23（PIN_I2S_DATA）、I2Sマスタで設定して
// s_sink.set_output(i2s) を呼び、set_stream_reader の第2引数を true にする [OI-06]。
// その際は "AudioTools.h" を "BluetoothA2DPSink.h" より先に include する（今出ている
// "AudioTools library is not included first" の #warning はこのため。I2S を使わない今は無害）。
// 注意: ライブラリは AVRCP の絶対音量を PCM に掛けてから出力する（音量は PCM5122 で制御する仕様。software.md 4.1）
static void rxOutputBegin() {
}

// ---- Bluetooth コールバック（BT タスク）----

static void onConnectionState(esp_a2d_connection_state_t state, void*) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    // peer_bd_addr はコールバック前にライブラリが接続相手のアドレスで更新している
    const uint8_t* a = *s_sink.get_current_peer_address();
    portENTER_CRITICAL(&s_mux);
    snprintf(s_mac, sizeof(s_mac), "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
    s_peerName[0] = '\0';
    clearMeta();
    s_evConnected = true;
    portEXIT_CRITICAL(&s_mux);
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    portENTER_CRITICAL(&s_mux);
    s_evDisconnected = true;
    portEXIT_CRITICAL(&s_mux);
  }
}

// AVRCP メタ情報（曲の変化時などにライブラリが要求し、属性ごとに届く。text は NUL 終端済み）
static void onMeta(uint8_t id, const uint8_t* text) {
  int i = metaIndex(id);
  if (i < 0) return;
  portENTER_CRITICAL(&s_mux);
  copyUtf8(s_meta[i].text, sizeof(s_meta[i].text), reinterpret_cast<const char*>(text));
  s_meta[i].valid = true;
  s_meta[i].dirty = true;
  portEXIT_CRITICAL(&s_mux);
}

// 接続後、GAP のリモート名取得の結果として非同期に届く
static void onPeerName(char* name) {
  portENTER_CRITICAL(&s_mux);
  strlcpy(s_peerName, name, sizeof(s_peerName));
  s_evPeerName = true;
  portEXIT_CRITICAL(&s_mux);
}

// ---- 公開関数 ----

void rxSetup() {
  rxOutputBegin();
  // 第2引数 false: ライブラリ既定の I2S 出力を使わない（init_i2s() で out->begin() が呼ばれない。
  // BluetoothA2DPSink.cpp の set_stream_reader / init_i2s / audio_data_callback で確認）
  s_sink.set_stream_reader(onPcm, false);
  s_sink.set_on_connection_state_changed(onConnectionState);
  s_sink.set_peer_name_callback(onPeerName);
  s_sink.set_avrc_metadata_attribute_mask(kMetaMask);
  s_sink.set_avrc_metadata_callback(onMeta);
  // 自動再接続はしない（既定 NoReconnect）。再接続は TX 側が行う（software.md 2.3）
  s_sink.start(BT_DEVICE_NAME);
  ledSet(LED_BLUE_BLINK);
  LOG1("A2DP sink started: %s", BT_DEVICE_NAME);
}

void rxLoop() {
  bool evConn, evDisc, evName;
  char mac[sizeof(s_mac)];
  char name[sizeof(s_peerName)];
  portENTER_CRITICAL(&s_mux);
  evConn = s_evConnected;
  evDisc = s_evDisconnected;
  evName = s_evPeerName;
  s_evConnected = s_evDisconnected = s_evPeerName = false;
  memcpy(mac, s_mac, sizeof(mac));
  memcpy(name, s_peerName, sizeof(name));
  portEXIT_CRITICAL(&s_mux);

  if (evConn) {
    s_connected = true;
    ledSet(LED_BLUE_ON);
    dialSendConnected(mac, "");  // ピア名は後から届く
  }
  if (evName && s_connected) {
    dialSendConnected(mac, name);
  }
  if (s_connected) sendMeta(true);
  if (evDisc && s_connected) {
    s_connected = false;
    ledSet(LED_BLUE_BLINK);
    dialSendDisconnected();
#if RESTART_ON_DISCONNECT
    // 切断後は Bluetooth スタック内の状態が残り音声が崩れるため、自身を再起動する（impl-notes 1.3）
    LOG1("restart on disconnect");
    Serial.flush();
    Serial1.flush();
    ESP.restart();
#endif
  }

#if LOG_LEVEL >= 2
  static uint32_t lastLog = 0;
  uint32_t now = millis();
  if (now - lastLog >= 1000) {
    uint32_t bytes = s_pcmBytes.exchange(0, std::memory_order_relaxed);
    if (s_connected) LOG2("pcm %lu B/s", (unsigned long)(bytes * 1000UL / (now - lastLog)));
    lastLog = now;
  }
#endif
}

void rxSendStatus() {
  if (!s_connected) {
    dialSendDisconnected();
    return;
  }
  char mac[sizeof(s_mac)];
  char name[sizeof(s_peerName)];
  portENTER_CRITICAL(&s_mux);
  memcpy(mac, s_mac, sizeof(mac));
  memcpy(name, s_peerName, sizeof(name));
  portEXIT_CRITICAL(&s_mux);
  dialSendConnected(mac, name);
  sendMeta(false);  // 受信済みのメタ情報
}

void rxKill() {
  if (!s_connected) {
    LOG1("KILL: not connected");
    return;
  }
  LOG1("KILL: disconnect");
  s_sink.disconnect();
}
