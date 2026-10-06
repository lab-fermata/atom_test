// tx_spdif.cpp — TX の音声入力: I2S（CS8416 からのスレーブ受信）→ 48k→44.1k の変換 → リングバッファ（software.md 2.1節・2.2節）
//
// - I2S は ESP-IDF の i2s_std を直接使う（コア 3.3.9 は IDF 5.5。driver/i2s_std.h）。CS8416 がマスタ、ESP32 はスレーブ。
//   BCK=G22、WS=G19、DIN=G23（hardware.md 4.1節）。フォーマットは I2S（Philips）、スロット幅 TX_I2S_SLOT_BITS [OI-01]
// - 受信は専用のタスク（コア1）で行う。i2s_channel_read() が TX_I2S_TIMEOUT_MS の間データを返さない、または
//   受信レート（LRCK）が入力のレートから外れていたら、エラーとして受信データを捨てる（BT には無音が渡る）。エラーが消えて TX_RECOVER_MS 正常が続いたら、
//   I2S を止めて再開してから送出を再開する（software.md 2.2節。L/R の入れ替わり・ビットずれの対策 [OI-05]）
// - 変換は窓付き sinc の多相 FIR（32 タップ × 64 相、相の間は線形補間、float）。比率は「入力のレート／44.1k」を、
//   リングバッファの量が目標（TX_RING_TARGET_MS）に保たれるように少しだけ変える（クロックのずれの吸収。software.md 2.1節）
// - リングバッファは書き込み（受信タスク）と読み出し（BT タスク）の単独所有。やり直し（空にする）は読み出し側が行う
//   （書き込み側は要求のフラグを立てるだけ。impl-notes.md 1.2節）
#include <Arduino.h>
#include <atomic>
#include <math.h>
#include <esp_timer.h>
#include <driver/i2s_std.h>
#include "tx_spdif.h"
#include "config.h"
#include "dial_link.h"

#if TX_AUDIO_SOURCE == 0
// 正弦波のモードでは使わない（静的メモリを取らないように中身を外す）
void spdifBegin() {}
int32_t spdifRead(Frame* data, int32_t len) {
  memset(data, 0, sizeof(Frame) * len);
  return len;
}
bool spdifError() { return false; }
const char* spdifErrorReason() { return "ok"; }
void spdifTakeStats(SpdifStats* s) { memset(s, 0, sizeof(*s)); }
#else

static constexpr int kOutRate = 44100;
static constexpr int kTaps = 32;
static constexpr int kPhases = 64;
static constexpr int kBlock = TX_I2S_BLOCK_FRAMES;
static constexpr uint32_t kRingCap = TX_RING_FRAMES;
static_assert((kRingCap & (kRingCap - 1)) == 0, "TX_RING_FRAMES must be a power of 2");
static constexpr uint32_t kRingTarget = (uint32_t)kOutRate * TX_RING_TARGET_MS / 1000;
static_assert(kRingTarget < kRingCap, "TX_RING_TARGET_MS is too large for TX_RING_FRAMES");
static constexpr uint32_t kRingPrime = (uint32_t)kOutRate * TX_RING_PRIME_MS / 1000;
static_assert(kRingPrime >= kRingTarget && kRingPrime < kRingCap, "TX_RING_PRIME_MS must be >= TX_RING_TARGET_MS");

// ---- リングバッファ（44.1kHz・16bit・2ch）----
static int16_t s_ring[kRingCap * 2];
static std::atomic<uint32_t> s_head{0};        // 書き込み位置（受信タスクだけが進める）
static std::atomic<uint32_t> s_tail{0};        // 読み出し位置（BT タスクだけが進める）
static std::atomic<bool> s_resetReq{true};     // 受信タスク → BT タスク: 空にして、目標の量まで貯め直す
static bool s_priming = true;                  // BT タスクだけが使う: 目標の量まで貯める間は無音を渡す
static std::atomic<uint32_t> s_lastReadMs{0};  // BT が最後にデータを要求した時刻（ms）

// ---- 状態・計測 ----
enum ErrReason : int { ERR_NONE = 0, ERR_TIMEOUT, ERR_RATE };
static std::atomic<uint32_t> s_rateHz{0};      // 最後に測った受信レート（Hz。エラー中も測る）
static std::atomic<uint32_t> s_rawFrames{0};   // 診断用: 受け取ったフレーム数（エラー中も数える。前回のログ以降）
static std::atomic<int> s_err{ERR_TIMEOUT};    // 起動直後はデータが来るまでエラー扱い
static std::atomic<uint32_t> s_inFrames{0}, s_outFrames{0}, s_readFrames{0}, s_underruns{0}, s_overruns{0};
static std::atomic<uint32_t> s_ringMin{UINT32_MAX}, s_ringMax{0}, s_procUsMax{0}, s_restarts{0};
static std::atomic<int32_t> s_adjPpm{0};

static void atomicMax(std::atomic<uint32_t>& a, uint32_t v) {
  uint32_t prev = a.load(std::memory_order_relaxed);
  while (v > prev && !a.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {
  }
}
static void atomicMin(std::atomic<uint32_t>& a, uint32_t v) {
  uint32_t prev = a.load(std::memory_order_relaxed);
  while (v < prev && !a.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {
  }
}

// ---- 変換（受信タスクだけが使う）----
static float s_coef[kPhases + 1][kTaps];       // s_coef[p][k] = h(k - (kTaps/2 - 1) - p/kPhases)
static float s_in[(kTaps + kBlock + 1) * 2];   // 入力の履歴（L,R の順）
static int s_inCount = 0;                      // s_in のフレーム数
static double s_pos = 0;                       // 次に作る出力の位置（s_in の先頭からの入力サンプル数）
static double s_stepNominal = 1.0;             // 入力のレート／出力のレート
static float s_fillAvg = 0;
static float s_adjInt = 0;                     // 比率の調整の積分の項（ppm）

static double besselI0(double x) {
  double sum = 1, term = 1;
  for (int k = 1; k < 30; k++) {
    term *= (x / (2 * k)) * (x / (2 * k));
    sum += term;
  }
  return sum;
}

// 係数を作る。遮断は 20.5kHz 付近（入力・出力の低い方のナイキストの 93%）、Kaiser 窓 β=6
static void buildCoef() {
  const double fc = 0.5 * fmin(1.0, (double)kOutRate / TX_IN_RATE) * 0.93;  // 入力のサンプルあたりの周波数
  const double beta = 6.0;
  const double half = kTaps / 2.0;
  const double i0b = besselI0(beta);
  for (int p = 0; p <= kPhases; p++) {
    double sum = 0;
    double row[kTaps];
    for (int k = 0; k < kTaps; k++) {
      double x = (k - (kTaps / 2 - 1)) - (double)p / kPhases;  // 中心からの距離（入力サンプル）
      double s = (x == 0) ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
      double r = x / half;
      double w = (fabs(r) >= 1) ? 0 : besselI0(beta * sqrt(1 - r * r)) / i0b;
      row[k] = s * w;
      sum += row[k];
    }
    for (int k = 0; k < kTaps; k++) s_coef[p][k] = (float)(row[k] / sum);  // 直流の利得を 1 にする
  }
}

static void resetConverter() {
  s_inCount = 0;
  s_pos = 0;
  s_fillAvg = kRingTarget;
  memset(s_in, 0, sizeof(s_in));
  s_resetReq.store(true, std::memory_order_release);
}

static inline int16_t toS16(float v) {
  float x = v * 32767.0f;
  if (x > 32767.0f) x = 32767.0f;
  if (x < -32768.0f) x = -32768.0f;
  return (int16_t)lrintf(x);
}

// 入力 n フレーム（float、L,R の順、±1.0）を変換してリングバッファに入れる
static void convert(const float* in, int n) {
  int64_t t0 = esp_timer_get_time();
  memcpy(&s_in[s_inCount * 2], in, sizeof(float) * 2 * n);
  s_inCount += n;
  s_inFrames.fetch_add(n, std::memory_order_relaxed);

  // 比率の調整: リングバッファの量（平滑化）が目標より多ければ入力を速く進める（出力を減らす）
  uint32_t head = s_head.load(std::memory_order_relaxed);
  uint32_t fill = head - s_tail.load(std::memory_order_acquire);
  // BT が読んでいない間（接続前など）は、変換の計算を省いて入力の位置だけ進め、空にする要求を出し続ける
  // （古いデータを貯めない。読み始めたら、新しいデータで目標の量まで貯めてから渡す）。比率の調整も止める
  bool reading = (uint32_t)(esp_timer_get_time() / 1000) - s_lastReadMs.load(std::memory_order_relaxed) < 50;
  if (!reading) {
    s_resetReq.store(true, std::memory_order_release);
    s_fillAvg = kRingTarget;
    const double step = s_stepNominal * (1.0 + s_adjInt * 1e-6);
    while ((int)s_pos + kTaps <= s_inCount) s_pos += step;
  }
  s_fillAvg += ((float)n / TX_IN_RATE * 1000.0f / TX_SRC_FILL_TAU_MS) * ((float)fill - s_fillAvg);
  float err = (s_fillAvg - kRingTarget) / kRingTarget;
  // PI: 比例だけだとクロックのずれの分だけ量が目標からずれたままになる（+200ppm で容量の近くまで増えた）ので、
  // 積分（時定数 TX_SRC_ADJ_TI_S 秒）でずれを吸収し、量を目標に戻す。準備中（目標の量まで貯める間）は積分しない
  // （積分の値はクロックのずれとして残す）
  float p = err * TX_SRC_ADJ_GAIN_PPM;
  if (reading && !s_resetReq.load(std::memory_order_relaxed)) {
    s_adjInt += p * ((float)n / TX_IN_RATE / TX_SRC_ADJ_TI_S);
    if (s_adjInt > TX_SRC_ADJ_MAX_PPM) s_adjInt = TX_SRC_ADJ_MAX_PPM;
    if (s_adjInt < -TX_SRC_ADJ_MAX_PPM) s_adjInt = -TX_SRC_ADJ_MAX_PPM;
  }
  float adj = p + s_adjInt;
  if (adj > TX_SRC_ADJ_MAX_PPM) adj = TX_SRC_ADJ_MAX_PPM;
  if (adj < -TX_SRC_ADJ_MAX_PPM) adj = -TX_SRC_ADJ_MAX_PPM;
  s_adjPpm.store((int32_t)lrintf(adj), std::memory_order_relaxed);
  const double step = s_stepNominal * (1.0 + adj * 1e-6);

  uint32_t written = 0;
  while ((int)s_pos + kTaps <= s_inCount) {
    int i0 = (int)s_pos;
    float fp = (float)(s_pos - i0) * kPhases;
    int p = (int)fp;
    float pf = fp - p;
    const float* c0 = s_coef[p];
    const float* c1 = s_coef[p + 1];
    const float* x = &s_in[i0 * 2];
    float l = 0, r = 0;
    for (int k = 0; k < kTaps; k++) {
      float c = c0[k] + (c1[k] - c0[k]) * pf;
      l += x[k * 2] * c;
      r += x[k * 2 + 1] * c;
    }
    s_pos += step;
    // リングバッファへ（あふれたら捨てる）
    if (head + written - s_tail.load(std::memory_order_acquire) >= kRingCap) {
      s_overruns.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    uint32_t i = (head + written) & (kRingCap - 1);
    s_ring[i * 2] = toS16(l);
    s_ring[i * 2 + 1] = toS16(r);
    written++;
  }
  s_head.store(head + written, std::memory_order_release);
  s_outFrames.fetch_add(written, std::memory_order_relaxed);

  // 使い終わった入力を捨てる（履歴の kTaps 分は残る）
  int used = (int)s_pos;
  if (used > 0) {
    memmove(s_in, &s_in[used * 2], sizeof(float) * 2 * (s_inCount - used));
    s_inCount -= used;
    s_pos -= used;
  }
  atomicMax(s_procUsMax, (uint32_t)(esp_timer_get_time() - t0));
}

// ---- 入力 ----
static float s_blockF[kBlock * 2];

#if TX_AUDIO_SOURCE == 1
#if TX_I2S_SLOT_BITS == 32
typedef int32_t SlotT;
static constexpr float kSlotScale = 1.0f / 2147483648.0f;
#define I2S_BITS I2S_DATA_BIT_WIDTH_32BIT
#elif TX_I2S_SLOT_BITS == 16
typedef int16_t SlotT;
static constexpr float kSlotScale = 1.0f / 32768.0f;
#define I2S_BITS I2S_DATA_BIT_WIDTH_16BIT
#else
#error "TX_I2S_SLOT_BITS must be 16 or 32"
#endif
static i2s_chan_handle_t s_rx = nullptr;
static SlotT s_rxBuf[kBlock * 2];

static bool i2sBegin() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
  chan.dma_desc_num = 4;
  chan.dma_frame_num = kBlock;
  if (i2s_new_channel(&chan, nullptr, &s_rx) != ESP_OK) return false;
  i2s_std_config_t std = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(TX_IN_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)PIN_I2S_BCK,
              .ws = (gpio_num_t)PIN_I2S_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = (gpio_num_t)PIN_I2S_DATA,
              .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
          },
  };
  if (i2s_channel_init_std_mode(s_rx, &std) != ESP_OK) return false;
  return i2s_channel_enable(s_rx) == ESP_OK;
}

// 受信レートの監視: エラー・時間切れに関係なく、受け取ったフレーム数（時間切れで一部だけ受け取った分も含む）を数え、
// TX_RATE_WINDOW_MS ごとに LRCK の周波数を求めて、エラーの判定に使う。
// CS8416 は PLL がアンロックすると、出力クロックが VCO の待機の周波数で出続け（データシートでは OLRCK 約 2.925kHz。
// AE-DIR8416 の実機・S/PDIF 未接続で約 2.7kHz）、クロックの切り替え（OMCK）が有効なら OMCK/256 になる
// （CS8416 データシート DS578F3 8.2節・表2）。以前は時間切れのたびに測り直していたので、遅いクロック（96 フレームに
// 約 35ms かかり、20ms で時間切れ）ではレートが一度も計算されなかった（rate 0 Hz。README の 2026-10-04 の状態確認）。
// いまは測り直さないので、1 回に読む量に関係なく検出できる。
// 窓の最初は DMA に溜まった分がすぐ読めるので、窓は長めにする（4×2ms の溜まりで 500ms の窓なら 1.6% 以内）
static bool s_rateBad = false;      // 最後の窓でレートが外れていた
static bool s_rateNoClock = false;  // 最後の窓でほとんど受け取らなかった（クロックが来ていない）
static uint32_t s_rateWinSeq = 0;   // 測った窓の数
static int64_t s_rateT0 = 0;
static uint32_t s_rateWin = 0;

static void rateUpdate(size_t gotBytes) {
  uint32_t frames = gotBytes / (sizeof(SlotT) * 2);
  s_rawFrames.fetch_add(frames, std::memory_order_relaxed);  // 診断用（1 秒ごとのログの raw）
  int64_t now = esp_timer_get_time();
  if (s_rateT0 == 0) {
    s_rateT0 = now;  // 最初の読み出しは時間の起点にするだけ
    return;
  }
  s_rateWin += frames;
  int64_t el = now - s_rateT0;
  if (el < TX_RATE_WINDOW_MS * 1000LL) return;
  uint32_t hz = (uint32_t)(s_rateWin * 1000000LL / el);
  s_rateHz.store(hz, std::memory_order_relaxed);
  s_rateBad = (uint32_t)abs((int)hz - TX_IN_RATE) > (uint32_t)TX_IN_RATE * TX_RATE_TOL_PCT / 100;
  s_rateNoClock = hz < (uint32_t)TX_IN_RATE / 100;
  s_rateWinSeq++;
  s_rateT0 = now;
  s_rateWin = 0;
}

static void taskI2s(void*) {
  int okMs = 0;
  uint32_t errSeq = 0;  // 最後にエラーだったときの窓の数
  for (;;) {
    size_t got = 0;
    esp_err_t e = i2s_channel_read(s_rx, s_rxBuf, sizeof(s_rxBuf), &got, TX_I2S_TIMEOUT_MS);
    rateUpdate(got);
    int reason = ERR_NONE;
    if (s_rateBad) {
      reason = s_rateNoClock ? ERR_TIMEOUT : ERR_RATE;  // ほとんど来ていなければ timeout、来ているが外れていれば rate
    } else if (e != ESP_OK || got < sizeof(s_rxBuf)) {
      reason = ERR_TIMEOUT;
    }
    int cur = s_err.load(std::memory_order_relaxed);
    if (reason != ERR_NONE) {
      okMs = 0;
      errSeq = s_rateWinSeq;
      if (cur != reason) s_err.store(reason, std::memory_order_relaxed);
      continue;  // 受信データは捨てる（BT には無音が渡る）
    }
    if (cur != ERR_NONE) {
      // エラーからの復帰: 正常が TX_RECOVER_MS 続き、最後のエラーの後に丸ごと正常な窓を1つ測って（errSeq+1 の窓は
      // エラーの間を含むことがあるので errSeq+2）レートが正しければ、I2S を止めて再開してから送出を再開する
      okMs += kBlock * 1000 / TX_IN_RATE;
      if (okMs < TX_RECOVER_MS || s_rateWinSeq < errSeq + 2) continue;
      i2s_channel_disable(s_rx);
      i2s_channel_enable(s_rx);
      s_restarts.fetch_add(1, std::memory_order_relaxed);
      resetConverter();
      s_err.store(ERR_NONE, std::memory_order_relaxed);
      continue;
    }
    for (int i = 0; i < kBlock * 2; i++) s_blockF[i] = s_rxBuf[i] * kSlotScale;
    convert(s_blockF, kBlock);
  }
}
#endif  // TX_AUDIO_SOURCE == 1

#if TX_AUDIO_SOURCE == 2
// 変換の試験: TX_IN_RATE×(1+TX_SRC_TEST_PPM) の速さで −20dBFS の正弦波（L=R）を作って変換に通す
static void taskTest(void*) {
  const double fs = TX_IN_RATE * (1.0 + TX_SRC_TEST_PPM * 1e-6);
  const double inc = 2 * M_PI * TX_SRC_TEST_HZ / TX_IN_RATE;  // 入力のサンプルあたりの位相
  const float amp = powf(10.0f, -20.0f / 20.0f);
  double phase = 0;
  int64_t t0 = esp_timer_get_time();
  uint64_t produced = 0;
  s_err.store(ERR_NONE, std::memory_order_relaxed);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(2));
    uint64_t due = (uint64_t)((esp_timer_get_time() - t0) * fs / 1e6);
    while (produced + kBlock <= due) {
      for (int i = 0; i < kBlock; i++) {
        float v = amp * (float)sin(phase);
        s_blockF[i * 2] = v;
        s_blockF[i * 2 + 1] = v;
        phase += inc;
        if (phase > 2 * M_PI) phase -= 2 * M_PI;
      }
      convert(s_blockF, kBlock);
      produced += kBlock;
    }
  }
}
#endif  // TX_AUDIO_SOURCE == 2

// ---- 公開関数 ----

void spdifBegin() {
  s_stepNominal = (double)TX_IN_RATE / kOutRate;
  buildCoef();
  resetConverter();
#if TX_AUDIO_SOURCE == 1
  bool ok = i2sBegin();
  LOG1("spdif: I2S slave %s (BCK G%d, WS G%d, DIN G%d, %d Hz, slot %d bit, timeout %d ms)", ok ? "ok" : "FAILED",
       PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DATA, TX_IN_RATE, TX_I2S_SLOT_BITS, TX_I2S_TIMEOUT_MS);
  if (ok) xTaskCreatePinnedToCore(taskI2s, "spdif", 4096, nullptr, 5, nullptr, 1);
#elif TX_AUDIO_SOURCE == 2
  LOG1("spdif: converter test (%d Hz tone at %d Hz %+d ppm)", TX_SRC_TEST_HZ, TX_IN_RATE, TX_SRC_TEST_PPM);
  xTaskCreatePinnedToCore(taskTest, "srctest", 4096, nullptr, 5, nullptr, 1);
#endif
  LOG1("spdif: %d -> %d Hz, %d taps x %d phases, ring %lu frames (target %lu = %d ms)", TX_IN_RATE, kOutRate, kTaps,
       kPhases, (unsigned long)kRingCap, (unsigned long)kRingTarget, TX_RING_TARGET_MS);
}

// BT タスクで呼ばれる。len フレームを必ず埋める
int32_t spdifRead(Frame* data, int32_t len) {
  s_lastReadMs.store((uint32_t)(esp_timer_get_time() / 1000), std::memory_order_relaxed);  // BT が読んでいる
  if (s_resetReq.exchange(false, std::memory_order_acquire)) {
    s_tail.store(s_head.load(std::memory_order_acquire), std::memory_order_release);
    s_priming = true;
  }
  uint32_t tail = s_tail.load(std::memory_order_relaxed);
  uint32_t avail = s_head.load(std::memory_order_acquire) - tail;
  bool err = s_err.load(std::memory_order_relaxed) != ERR_NONE;
  // 貯め直すときは目標より多め（TX_RING_PRIME_MS）まで貯める（音声の開始の直後に BT が多めに読み、目標の量では
  // 1回足りなくなった。多い分は比率の調整で目標に戻る）
  if (s_priming && !err && avail >= kRingPrime) s_priming = false;
  if (s_priming || err) {
    memset(data, 0, sizeof(Frame) * len);
    return len;
  }
  atomicMin(s_ringMin, avail);
  atomicMax(s_ringMax, avail);
  uint32_t n = avail < (uint32_t)len ? avail : (uint32_t)len;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t j = (tail + i) & (kRingCap - 1);
    data[i].channel1 = s_ring[j * 2];
    data[i].channel2 = s_ring[j * 2 + 1];
  }
  s_tail.store(tail + n, std::memory_order_release);
  s_readFrames.fetch_add(n, std::memory_order_relaxed);
  if (n < (uint32_t)len) {
    // 足りない: 残りは無音にし、目標の量まで貯め直す
    memset(&data[n], 0, sizeof(Frame) * (len - n));
    s_underruns.fetch_add(1, std::memory_order_relaxed);
    s_priming = true;
  }
  return len;
}

bool spdifError() { return s_err.load(std::memory_order_relaxed) != ERR_NONE; }

const char* spdifErrorReason() {
  switch (s_err.load(std::memory_order_relaxed)) {
    case ERR_NONE: return "ok";
    case ERR_TIMEOUT: return "timeout";
    case ERR_RATE: return "rate";
    default: return "?";
  }
}

void spdifTakeStats(SpdifStats* s) {
  s->inFrames = s_inFrames.exchange(0, std::memory_order_relaxed);
  s->outFrames = s_outFrames.exchange(0, std::memory_order_relaxed);
  s->readFrames = s_readFrames.exchange(0, std::memory_order_relaxed);
  s->underruns = s_underruns.exchange(0, std::memory_order_relaxed);
  s->overruns = s_overruns.exchange(0, std::memory_order_relaxed);
  uint32_t mn = s_ringMin.exchange(UINT32_MAX, std::memory_order_relaxed);
  s->ringMin = (mn == UINT32_MAX) ? 0 : mn;
  s->ringMax = s_ringMax.exchange(0, std::memory_order_relaxed);
  s->procUsMax = s_procUsMax.exchange(0, std::memory_order_relaxed);
  s->restarts = s_restarts.load(std::memory_order_relaxed);
  s->adjPpm = s_adjPpm.load(std::memory_order_relaxed);
  s->rateHz = s_rateHz.load(std::memory_order_relaxed);
  s->rawFrames = s_rawFrames.exchange(0, std::memory_order_relaxed);
}
#endif  // TX_AUDIO_SOURCE != 0
