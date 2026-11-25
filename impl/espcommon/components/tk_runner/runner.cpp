#include "tk_runner.hpp"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <new>

#include "net.hpp"
#include "pipeline.hpp"
#include "serial.hpp"
#include "shapoco/gfx2d/graphics2d.hpp"

// 0 なら全部の帯を core0 だけで描く (比較用)。idf.py -DTKAPP_DUAL_CORE=0 build
#ifndef TKAPP_DUAL_CORE
#define TKAPP_DUAL_CORE 1
#endif

namespace tkrunner {

namespace g2 = shapoco::gfx2d;

namespace {

constexpr size_t ARENA_BYTES = 56 * 1024;   // Graphics3D の作業領域 (内蔵 RAM)。三角形は最大で 32 KB ほど使う
constexpr size_t GFX2D_ARENA_BYTES = 6144;
constexpr int64_t LOG_INTERVAL_US = 2000000;
constexpr int PIPELINE_CORE = 1;
constexpr int PIPELINE_PRIORITY = 2;  // WiFi と lwIP (18 以上) に譲る
constexpr int RENDER_CORES = TKAPP_DUAL_CORE ? 2 : 1;
constexpr int WORKER_CORE = 1;
constexpr int WORKER_PRIORITY = 3;    // パイプラインより上、WiFi と lwIP より下。帯を待つ間はパイプラインが動く
constexpr uint32_t WORKER_STACK_BYTES = 8 * 1024;
constexpr size_t INTERNAL_RESERVE = 40 * 1024;  // 作業用のメモリを内蔵 RAM に置くとき、WiFi などに残す量

class EspPlatform : public tkapp::Platform {
 public:
  void applyWifiConfig(const tkapp::WifiConfig &cfg) override { netApplyConfig(cfg); }
  tkapp::WifiState wifiState() override { return demo_ ? tkapp::WifiState::UNAVAILABLE : netState(); }
  bool fetching() override { return tkrunner::pipelineFetching(); }
  int viewerCount() override { return tkrunner::pipelineViewerCount(); }
  void log(const char *msg) override { std::printf("app: %s\n", msg); }
  bool demo_ = false;
};

EspPlatform g_platform;
tkapp::App *g_app = nullptr;
// 2D の描画 (文字、UI) はコアごとに別のコンテキストと作業領域を使う
g2::Graphics2D g_gfx[2];
alignas(8) uint8_t g_gfxArena[GFX2D_ARENA_BYTES];   // core0 用。core1 用は起動時に確保する

// core1 が描く帯の下半分。core0 が書いてから通知し、core1 は描き終えて通知を返すまで core0 は触らない
struct Job {
  uint16_t *pixels;
  int y, rows;
};
Job g_job;
int64_t g_workerUs = 0;  // core1 が描いた時間 (core1 → core0、同様)
TaskHandle_t g_mainTask = nullptr;
TaskHandle_t g_workerTask = nullptr;
int g_w = 0;

struct Stats {
  int frames = 0;
  int64_t pollUs = 0, advanceUs = 0, sceneUs = 0, drawUs = 0, draw1Us = 0, joinUs = 0, drainUs = 0, submitUs = 0;
  int64_t since = 0;
};

[[noreturn]] void halt(const char *msg) {
  for (;;) {
    std::printf("%s\n", msg);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void *allocInternal(size_t bytes, const char *what) {
  void *p = heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) {
    std::printf("%s (%u bytes) did not fit in internal RAM, largest block %u\n", what, (unsigned)bytes,
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  return p;
}

void *allocAny(size_t bytes, const char *what, const char **where) {
  void *p = heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  *where = "PSRAM";
  if (!p) {
    p = heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    *where = "internal RAM";
  }
  if (!p) std::printf("%s (%u bytes) did not fit\n", what, (unsigned)bytes);
  return p;
}

void logStats(Stats &s, int64_t now) {
  if (s.frames == 0) return;
  const int n = s.frames;
  const float fps = n * 1e6f / (float)(now - s.since);
  const int fps10 = (int)(fps * 10 + 0.5f);
  const tkapp::Stats &as = g_app->stats();
  const PipelineStatus &ps = pipelineStatus();
  std::printf("%d.%d fps, us/frame: poll %lld, advance %lld, scene %lld, draw core0 %lld core1 %lld, wait core1 %lld, "
              "wait panel %lld, submit %lld; "
              "tris %u, span peak %u, tri bytes %u; internal free %u; "
              "pipeline fetch %lu ok %lu same %lu few %lu err %lu, last fetch %lu ms fit %lu ms\n",
              fps10 / 10, fps10 % 10, s.pollUs / n, s.advanceUs / n, s.sceneUs / n, s.drawUs / n, s.draw1Us / n,
              s.joinUs / n, s.drainUs / n,
              s.submitUs / n, (unsigned)as.triCount,
              (unsigned)as.spanPeak, (unsigned)as.triBytes, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              (unsigned long)ps.fetches, (unsigned long)ps.ok, (unsigned long)ps.unchanged, (unsigned long)ps.fewLit,
              (unsigned long)ps.errors, (unsigned long)ps.lastFetchMs, (unsigned long)ps.lastFitMs);
  s = Stats();
  s.since = now;
}

// 帯 (の一部) を描く。ctx は描画コンテキスト (= コア)
void renderRows(int ctx, uint16_t *pixels, int y, int rows) {
  if (rows <= 0) return;
  const g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, g_w, rows, pixels);
  g_app->renderStrip(ctx, g_gfx[ctx], s, y);
}

void workerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const int64_t t0 = esp_timer_get_time();
    renderRows(1, g_job.pixels, g_job.y, g_job.rows);
    g_workerUs = esp_timer_get_time() - t0;
    xTaskNotifyGive(g_mainTask);
  }
}

// 内蔵 RAM に余裕があればそこに、なければ PSRAM に置く
void *allocPreferInternal(size_t bytes, const char *what) {
  void *p = nullptr;
  const char *where = "internal RAM";
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= bytes &&
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= bytes + INTERNAL_RESERVE) {
    p = heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!p) {
    p = heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    where = "PSRAM";
  }
  if (p) std::printf("%s %u bytes in %s\n", what, (unsigned)bytes, where);
  else std::printf("%s (%u bytes) did not fit\n", what, (unsigned)bytes);
  return p;
}

// core1 で帯の下半分を描くタスクを作る。パイプラインと WiFi の後に作り、内蔵 RAM の残りを見て置き場所を決める
bool startWorker() {
  void *arena = allocPreferInternal(GFX2D_ARENA_BYTES, "core1 2D arena");
  // スタックを PSRAM に置けるのは CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY による。このタスクは NVS を書かない
  void *stack = allocPreferInternal(WORKER_STACK_BYTES, "core1 render stack");
  if (!arena || !stack) return false;
  g_gfx[1].init(arena, GFX2D_ARENA_BYTES);
  g_mainTask = xTaskGetCurrentTaskHandle();
  static StaticTask_t tcb;
  g_workerTask = xTaskCreateStaticPinnedToCore(workerTask, "tk_render1", WORKER_STACK_BYTES, nullptr, WORKER_PRIORITY,
                                               (StackType_t *)stack, &tcb, WORKER_CORE);
  return g_workerTask != nullptr;
}

}  // namespace

void run(Board &board, const Config &cfg) {
  int w = 0, h = 0;
  if (!board.init(cfg, w, h)) halt("board init failed");
  const int stripH = std::min(cfg.stripH, h);
  std::printf(TKAPP_DISP_NAME " on %s: frame %dx%d, strips of %d rows%s\n", cfg.name, w, h, stripH,
              cfg.demo ? ", demo" : "");
  std::printf("internal free %u, largest block %u, PSRAM free %u\n",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  // 置き場所: テクスチャと 3D の arena は内蔵 RAM、App と履歴は PSRAM (無ければ内蔵)
  // App は PSRAM でも速度が変わらない (毎ピクセル触るのはテクスチャと arena だけ) ので内蔵 RAM を WiFi に残す
  const char *where;
  void *appMem = allocAny(sizeof(tkapp::App), "app", &where);
  if (!appMem) halt("app did not fit");
  std::printf("app %u bytes in %s\n", (unsigned)sizeof(tkapp::App), where);
  g_app = new (appMem) tkapp::App();
  tkapp::Buffers buffers;
  buffers.history = (tkapp::EventRecord *)allocAny(sizeof(tkapp::EventRecord) * tkapp::HISTORY_MAX, "history", &where);
  buffers.historyCapacity = tkapp::HISTORY_MAX;
  std::printf("history %u bytes in %s\n", (unsigned)(sizeof(tkapp::EventRecord) * tkapp::HISTORY_MAX), where);
  buffers.texturePixels = (uint16_t *)allocInternal(sizeof(uint16_t) * tkapp::TEXTURE_PIXELS, "textures");
  void *arena = allocInternal(ARENA_BYTES, "3D arena");
  if (!buffers.history || !buffers.texturePixels || !arena) halt("buffers did not fit");

  g_platform.demo_ = cfg.demo;
  if (!g_app->init(w, h, buffers, &g_platform, arena, ARENA_BYTES, RENDER_CORES)) halt("app init failed");
  g_app->setMultiTouch(cfg.multiTouch);
  g_gfx[0].init(g_gfxArena, sizeof g_gfxArena);
  g_w = w;

  if (!cfg.demo) {
    netStart();
    tkapp::WifiConfig wc;
    if (netLoadConfig(wc)) g_app->setWifiConfig(wc);
    serialStart(PIPELINE_CORE);
  }
  PipelineConfig pc;
  pc.demo = cfg.demo;
  pc.intervalSec = cfg.demo ? cfg.demoIntervalSec : tkapp::FETCH_INTERVAL_SEC;
  pc.core = PIPELINE_CORE;
  pc.priority = PIPELINE_PRIORITY;
  if (!pipelineStart(pc)) halt("pipeline not started");
  if (RENDER_CORES > 1 && !startWorker()) halt("render worker not started");
  std::printf("rendering on %d core(s)\n", RENDER_CORES);
  std::printf("internal free after setup %u, largest block %u\n",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  PanelOut &out = board.panel();
  {
    // パネルへの転送だけの時間 (黒い帯を 1 フレーム分)
    const int64_t m0 = esp_timer_get_time();
    for (int y = 0, i = 0; y < h; y += stripH, i ^= 1) {
      out.drain();
      out.submit(i, y, std::min(stripH, h - y));
    }
    out.drain();
    std::printf("panel transfer: %lld us per frame\n", (long long)(esp_timer_get_time() - m0));
  }
  const int64_t start = esp_timer_get_time();
  int64_t lastT = start;
  Stats stats;
  stats.since = start;
  int cur = 0;
  for (;;) {
    const int64_t t0 = esp_timer_get_time();
    // 表示の時刻が来たイベントを受け取る
    while (tkapp::EventRecord *rec = pipelinePoll()) {
      g_app->pushEvent(*rec);
      pipelineRelease(rec);
    }
    // イベントの記録が更新されていたら最下部の画像を作り直す
    {
      static uint32_t logGen = 0;
      pipelineVisitLog(logGen, [](const tkapp::EventLog &log, void *) { g_app->updateLog(log); }, nullptr);
    }

    board.poll(*g_app);
    {
      tkapp::WifiConfig wc;
      if (netTakeUpdatedConfig(wc)) g_app->setWifiConfig(wc);  // シリアルで変えた設定を画面側にも
    }
    const int64_t tp = esp_timer_get_time();
    g_app->advance((float)(t0 - lastT) * 1e-6f);
    lastT = t0;
    const int64_t ta = esp_timer_get_time();
    g_app->buildScene();
    const int64_t t1 = esp_timer_get_time();
    stats.pollUs += tp - t0;
    stats.advanceUs += ta - tp;
    stats.sceneUs += t1 - ta;

    // 帯 idx に描いている間、もう一方の帯は転送中。描き終えてからその転送を待ち、idx を送る。
    // 2 コアのときは帯の上半分を core0 (このタスク)、下半分を core1 (workerTask) が同時に描く
    for (int y = 0; y < h; y += stripH) {
      const int rows = std::min(stripH, h - y);
      const int idx = cur;
      cur ^= 1;
      uint16_t *buf = out.buffer(idx);
      const int64_t d0 = esp_timer_get_time();
      int64_t d1;
      if (RENDER_CORES > 1) {
        const int upper = rows / 2;
        g_job = {buf + upper * w, y + upper, rows - upper};
        xTaskNotifyGive(g_workerTask);
        renderRows(0, buf, y, upper);
        d1 = esp_timer_get_time();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        stats.draw1Us += g_workerUs;
      } else {
        renderRows(0, buf, y, rows);
        d1 = esp_timer_get_time();
      }
      const int64_t d2 = esp_timer_get_time();
      out.drain();
      const int64_t d3 = esp_timer_get_time();
      out.submit(idx, y, rows);
      stats.drawUs += d1 - d0;
      stats.joinUs += d2 - d1;
      stats.drainUs += d3 - d2;
      stats.submitUs += esp_timer_get_time() - d3;
    }
    g_app->endFrame();
    stats.frames++;
    const int64_t now = esp_timer_get_time();
    if (now - stats.since >= LOG_INTERVAL_US) logStats(stats, now);
  }
}

}  // namespace tkrunner
