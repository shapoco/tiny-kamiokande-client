#include "pipeline.hpp"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <new>


#include "net.hpp"
#include "tkapp/cbor.hpp"
#include "tkapp/config.hpp"
#include "tkapp/sync.hpp"

// CMakeLists.txt で埋め込むデモ用のデータ
extern const uint8_t g_s1Start[] asm("_binary_s1_cbor_start");
extern const uint8_t g_s1End[] asm("_binary_s1_cbor_end");
extern const uint8_t g_s2Start[] asm("_binary_s2_cbor_start");
extern const uint8_t g_s2End[] asm("_binary_s2_cbor_end");
extern const uint8_t g_s3Start[] asm("_binary_s3_cbor_start");
extern const uint8_t g_s3End[] asm("_binary_s3_cbor_end");

namespace tkrunner {

namespace {

constexpr int SLOTS = 5;  // 表示待ち (15 秒 / 9 秒 ≒ 2 件) + 取得中の 1 件 + 余裕 2 件
constexpr size_t FETCH_BUF_BYTES = 96 * 1024;  // api.php?fmt=bin は約 26 KB
constexpr uint32_t STACK_BYTES = 12 * 1024;  // 通信 (TLS の握手) と CBOR の展開

PipelineConfig g_cfg;
tkapp::EventRecord *g_slots = nullptr;
uint8_t *g_fetchBuf = nullptr;
QueueHandle_t g_freeQ = nullptr, g_readyQ = nullptr;
PipelineStatus g_status = {};
volatile bool g_fetching = false;       // api.php から取得している最中
uint32_t g_lastCrc = 0;
// イベントの記録。パイプラインのタスクが応答を取り込み、描画側が pipelineVisitLog で読む (g_logMutex で排他)
tkapp::EventLog *g_log = nullptr;
SemaphoreHandle_t g_logMutex = nullptr;
volatile uint32_t g_logGen = 0;
constexpr size_t URL_MAX = 160;
volatile int g_viewers = -1;          // 最後に受け取った接続数 (api.php の clients)。-1 なら不明
volatile int64_t g_viewersAtUs = 0;   // それを受け取った時刻
constexpr int64_t VIEWERS_STALE_US = 60 * 1000000LL;  // これより古い接続数は出さない (通信が途絶えたとき)

// 表示待ちのキューの時刻の判定 (tkapp/sync.hpp)。パイプラインのタスク (core1) が標本を入れ、
// pipelinePoll (core0) が判定するので g_syncMux で排他する
portMUX_TYPE g_syncMux = portMUX_INITIALIZER_UNLOCKED;
tkapp::DisplaySync g_sync;

// 毎回 TLS の握手から始める (keep-alive なし) ので、Web 版のような往復の中点は握手の分だけずれる。
// ヘッダが届いた時刻との差を使う (誤差は下りの片道程度。DisplaySync::noteServerTime)
void noteServerTime(int64_t serverTimeMs, const NetTiming &t) {
  taskENTER_CRITICAL(&g_syncMux);
  g_sync.noteServerTime(serverTimeMs, t.sentUs, t.headersUs);
  taskEXIT_CRITICAL(&g_syncMux);
}

// 表示の時刻が来たか。デモ、サーバ時刻が分からない (古いサーバ) ときはすぐ表示する
bool isDue(const tkapp::EventRecord &rec) {
  if (g_cfg.demo) return true;
  taskENTER_CRITICAL(&g_syncMux);
  const bool due = g_sync.isDue(rec.data.timestampMs, esp_timer_get_time());
  taskEXIT_CRITICAL(&g_syncMux);
  return due;
}

struct Sample {
  const uint8_t *start, *end;
};
const Sample SAMPLES[] = {{g_s1Start, g_s1End}, {g_s2Start, g_s2End}, {g_s3Start, g_s3End}};
constexpr int SAMPLE_COUNT = (int)(sizeof(SAMPLES) / sizeof(SAMPLES[0]));

void *allocPreferSpiram(size_t bytes, const char *what) {
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) std::printf("pipeline: %s (%u bytes) did not fit\n", what, (unsigned)bytes);
  return p;
}

void pipelineTask(void *) {
  int sampleIndex = 0;
  int64_t nextFetchUs = esp_timer_get_time();
  for (;;) {
    const int64_t now = esp_timer_get_time();
    if (now < nextFetchUs) {
      vTaskDelay(pdMS_TO_TICKS((nextFetchUs - now) / 1000 + 1));
      continue;
    }
    nextFetchUs = now + (int64_t)(g_cfg.intervalSec * 1e6f);
    tkapp::EventRecord *rec = nullptr;
    if (xQueueReceive(g_freeQ, &rec, portMAX_DELAY) != pdTRUE || !rec) continue;

    // 取得
    const uint8_t *data = nullptr;
    size_t bytes = 0;
    NetTiming timing;
    const int64_t t0 = esp_timer_get_time();
    g_status.fetches++;
    if (g_cfg.demo) {
      const Sample &s = SAMPLES[sampleIndex];
      sampleIndex = (sampleIndex + 1) % SAMPLE_COUNT;
      data = s.start;
      bytes = (size_t)(s.end - s.start);
    } else if (netState() == tkapp::WifiState::CONNECTED) {
      // 記録は前回の応答の現在の枠から後ろだけを要求する (初回は直近 80 時間分)
      char url[URL_MAX];
      std::snprintf(url, sizeof url, "%s&log=%lld", TKAPP_API_URL, (long long)g_log->requestFrom());
      g_fetching = true;
      const int n = netFetch(url, g_fetchBuf, FETCH_BUF_BYTES, &g_status.lastHttpStatus, &timing);
      g_fetching = false;
      if (n > 0) {
        data = g_fetchBuf;
        bytes = (size_t)n;
      }
    }
    g_status.lastFetchMs = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (!data) {
      if (!g_cfg.demo && netState() == tkapp::WifiState::CONNECTED) g_status.errors++;
      xQueueSend(g_freeQ, &rec, 0);
      continue;
    }

    // 展開 (推定結果はサーバが応答に入れて返す)
    tkapp::ApiInfo info;
    const tkapp::ApiStatus st = tkapp::decodeApiResponse(data, bytes, *rec, info);
    if (!g_cfg.demo) {
      noteServerTime(info.serverTimeMs, timing);
      if (st != tkapp::ApiStatus::MALFORMED && info.logNow > 0) {
        xSemaphoreTake(g_logMutex, portMAX_DELAY);
        g_log->merge(info.logNow, info.logRows, info.logBytes);
        g_logGen = g_logGen + 1;
        xSemaphoreGive(g_logMutex);
      }
      if (info.clients >= 0) {
        g_viewersAtUs = esp_timer_get_time();
        g_viewers = (int)info.clients;
      }
    }
    bool keep = false;
    if (st == tkapp::ApiStatus::OK) {
      if (!g_cfg.demo && rec->data.crc32 == g_lastCrc) {
        g_status.unchanged++;
      } else {
        g_lastCrc = rec->data.crc32;
        keep = true;
      }
    } else if (st == tkapp::ApiStatus::FEW_LIT) {
      g_status.fewLit++;
    } else {
      g_status.errors++;
      std::printf("pipeline: bad response (status %d, code %d, '%s')\n", (int)st, info.statusCode, info.errorMsg);
    }
    if (!keep) {
      xQueueSend(g_freeQ, &rec, 0);
      continue;
    }
    g_status.lastFitMs = rec->fit.fitMs;
    g_status.ok++;
    taskENTER_CRITICAL(&g_syncMux);
    g_sync.startRamp(esp_timer_get_time());
    taskEXIT_CRITICAL(&g_syncMux);
    xQueueSend(g_readyQ, &rec, portMAX_DELAY);
  }
}

}  // namespace

bool pipelineStart(const PipelineConfig &cfg) {
  g_cfg = cfg;
  g_slots = (tkapp::EventRecord *)allocPreferSpiram(sizeof(tkapp::EventRecord) * SLOTS, "slots");
  if (!cfg.demo) g_fetchBuf = (uint8_t *)allocPreferSpiram(FETCH_BUF_BYTES, "fetch buffer");
  void *logMem = allocPreferSpiram(sizeof(tkapp::EventLog), "event log");
  g_logMutex = xSemaphoreCreateMutex();
  if (!g_slots || (!cfg.demo && !g_fetchBuf) || !logMem || !g_logMutex) {
    std::printf("pipeline: allocation failed (slots %d, fetch %d, log %d)\n", g_slots != nullptr, g_fetchBuf != nullptr,
                logMem != nullptr);
    return false;
  }
  g_log = new (logMem) tkapp::EventLog();

  g_freeQ = xQueueCreate(SLOTS, sizeof(tkapp::EventRecord *));
  g_readyQ = xQueueCreate(SLOTS, sizeof(tkapp::EventRecord *));
  if (!g_freeQ || !g_readyQ) {
    std::printf("pipeline: queues not created\n");
    return false;
  }
  for (int i = 0; i < SLOTS; i++) {
    tkapp::EventRecord *p = &g_slots[i];
    xQueueSend(g_freeQ, &p, 0);
  }
  // スタックは PSRAM (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)。このタスクは Flash の書き込み (NVS) をしない
  static StaticTask_t tcb;
  void *stack = allocPreferSpiram(STACK_BYTES, "task stack");
  if (!stack) return false;
  if (!xTaskCreateStaticPinnedToCore(pipelineTask, "tk_pipe", STACK_BYTES, nullptr, cfg.priority, (StackType_t *)stack,
                                     &tcb, cfg.core)) {
    std::printf("pipeline: task not created\n");
    return false;
  }
  return true;
}

// 先頭のイベントの表示の時刻が来ていれば取り出す (受け取るのは描画側だけなので、覗いてから取り出してよい)
tkapp::EventRecord *pipelinePoll() {
  tkapp::EventRecord *rec = nullptr;
  if (!g_readyQ || xQueuePeek(g_readyQ, &rec, 0) != pdTRUE || !rec) return nullptr;
  if (!isDue(*rec)) return nullptr;
  if (xQueueReceive(g_readyQ, &rec, 0) != pdTRUE) return nullptr;
  return rec;
}

void pipelineRelease(tkapp::EventRecord *rec) {
  if (rec && g_freeQ) xQueueSend(g_freeQ, &rec, 0);
}

bool pipelineVisitLog(uint32_t &gen, void (*fn)(const tkapp::EventLog &log, void *user), void *user) {
  if (!g_log || !g_logMutex || gen == g_logGen) return false;
  xSemaphoreTake(g_logMutex, portMAX_DELAY);
  gen = g_logGen;
  fn(*g_log, user);
  xSemaphoreGive(g_logMutex);
  return true;
}

bool pipelineFetching() { return g_fetching; }

const PipelineStatus &pipelineStatus() { return g_status; }

int pipelineViewerCount() {
  const int n = g_viewers;
  if (n < 0 || esp_timer_get_time() - g_viewersAtUs > VIEWERS_STALE_US) return -1;
  return n;
}

}  // namespace tkrunner
