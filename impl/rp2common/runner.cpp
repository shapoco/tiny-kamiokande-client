#include "runner.hpp"

#include <malloc.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

#include "clocks.hpp"
#include "console.hpp"
#include "diag.hpp"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "net.hpp"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pipeline.hpp"
#include "settings_store.hpp"
#include "shapoco/gfx2d/graphics2d.hpp"
#include "tkapp/detector.hpp"

#ifndef RP2COMMON_LCD_SPI
#error "the board header does not name the panel pins (RP2COMMON_LCD_*)"
#endif
#ifndef TKRP2_DUAL_CORE
#define TKRP2_DUAL_CORE 1
#endif
// パネルの SPI クロック: clk_peri (clk_sys / 2 = 150 MHz) / 2
#ifndef RP2COMMON_LCD_BAUD
#define RP2COMMON_LCD_BAUD 75000000
#endif
#ifndef RP2COMMON_LCD_FLIP
#define RP2COMMON_LCD_FLIP 0
#endif
#ifndef RP2COMMON_LCD_CMD_BAUD
#define RP2COMMON_LCD_CMD_BAUD 0
#endif
#ifndef RP2COMMON_BACKLIGHT
#define RP2COMMON_BACKLIGHT 200
#endif

// リンカスクリプトの記号 (ヒープの残りを出すため)
extern "C" char __StackLimit;

namespace tkrp2 {

namespace g2 = shapoco::gfx2d;

namespace {

constexpr size_t ARENA_BYTES = 56 * 1024;  // Graphics3D の作業領域。三角形は最大で 32 KB ほど使う (ESP 版と同じ)
constexpr size_t GFX2D_ARENA_BYTES = 6144;
constexpr int RENDER_CORES = TKRP2_DUAL_CORE ? 2 : 1;
constexpr uint64_t LOG_INTERVAL_US = 2000000;
// core1 のスタック。帯の描画に加えて、WiFi と lwIP の割り込み (TLS の握手の計算を含む) もここで動く
constexpr size_t CORE1_STACK_BYTES = 16 * 1024;
constexpr uint64_t CORE1_READY_TIMEOUT_US = 10000000;

class Rp2Platform : public tkapp::Platform {
 public:
  void applyWifiConfig(const tkapp::WifiConfig &cfg) override;
  tkapp::WifiState wifiState() override { return demo_ ? tkapp::WifiState::UNAVAILABLE : netState(); }
  bool fetching() override { return pipelineFetching(); }
  int viewerCount() override { return pipelineViewerCount(time_us_64()); }
  void log(const char *msg) override { std::printf("app: %s\n", msg); }
  bool demo_ = false;
};

// 大きなものは静的に置く (リンク時に RAM の使用量が分かる。ヒープは mbedTLS が使う)
Rp2Platform g_platform;
tkapp::App g_app;
tkapp::EventRecord g_history[tkapp::HISTORY_MAX];
alignas(8) uint16_t g_texture[tkapp::TEXTURE_PIXELS];
alignas(8) uint8_t g_arena3d[ARENA_BYTES];
// 2D の描画 (文字、UI) はコアごとに別のコンテキストと作業領域を使う
g2::Graphics2D g_gfx[2];
alignas(8) uint8_t g_gfxArena[2][GFX2D_ARENA_BYTES];
rp2common::SpiPanel g_panel;
int g_w = 0, g_h = 0;
bool g_demo = false;

// WiFi の設定。保存は Flash を書くのでフレームの合間に行う
tkapp::WifiConfig g_wifi;
bool g_savePending = false;

// core1 が描く行。core0 が FIFO に push する前に書き、core1 が pop した後に読む
struct Job {
  uint16_t *pixels;
  int y, rows;
};
Job g_job;
// core0 が FIFO で送る指示
constexpr uint32_t CORE1_DRAW = 0;  // g_job を描く
constexpr uint32_t CORE1_PARK = 1;  // Flash を書き終わるまで待つ
volatile bool g_core1Parked = false;
volatile bool g_core1Release = false;
volatile bool g_core1Ready = false;
volatile bool g_netOk = false;
// PICO_CORE1_STACK_SIZE は 0 にして、SCRATCH_X は core0 のスタックに譲る
uint32_t g_core1Stack[CORE1_STACK_BYTES / 4];

void renderRows(int core, uint16_t *pixels, int y, int rows) {
  if (rows <= 0) return;
  const g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, g_w, rows, pixels);
  g_app.renderStrip(core, g_gfx[core], s, y);
}

// Flash を書いている間は XIP が使えないので、割り込み (WiFi を含む) を止めて RAM 上で待つ。
// SDK の multicore_lockout は FIFO の値を割り込みで読み捨てるので、帯の受け渡しと共存できない
void __no_inline_not_in_flash_func(core1Park)() {
  const uint32_t irq = save_and_disable_interrupts();
  g_core1Parked = true;
  while (!g_core1Release) tight_loop_contents();
  g_core1Parked = false;
  restore_interrupts(irq);
}

void core1Main() {
  // WiFi の割り込みをこのコアで受けるため、CYW43 はここで初期化する
  if (!g_demo) g_netOk = netInitOnThisCore();
  __dmb();
  g_core1Ready = true;
  for (;;) {
    rp2common::diagMark(1, rp2common::Phase::IDLE, 0);
    const uint32_t cmd = multicore_fifo_pop_blocking();
    __dmb();
    if (cmd == CORE1_PARK) {
      core1Park();
      continue;
    }
    rp2common::diagMark(1, rp2common::Phase::DRAW, g_job.y);
    const uint64_t t0 = time_us_64();
    renderRows(1, g_job.pixels, g_job.y, g_job.rows);
    __dmb();
    multicore_fifo_push_blocking((uint32_t)(time_us_64() - t0));
  }
}

// フレームの合間 (core1 が次の帯を待っている間) に core0 から呼ぶ
void saveSettings() {
  g_savePending = false;
  g_core1Release = false;
  __dmb();
  multicore_fifo_push_blocking(CORE1_PARK);
  while (!g_core1Parked) tight_loop_contents();
  const uint64_t t0 = time_us_64();
  const bool ok = settingsSave(g_wifi);
  const uint64_t us = time_us_64() - t0;
  g_core1Release = true;
  // 次に止めるときに g_core1Release を下ろしても取りこぼさないよう、抜けるのを待つ
  while (g_core1Parked) tight_loop_contents();
  std::printf("settings: %s (%llu ms)\n", ok ? "saved" : "write failed", (unsigned long long)(us / 1000));
}

void applyConfig(const tkapp::WifiConfig &cfg) {
  g_wifi = cfg;
  g_app.setWifiConfig(cfg);
  g_savePending = true;
  if (!g_demo) netSetConfig(cfg);
}

void Rp2Platform::applyWifiConfig(const tkapp::WifiConfig &cfg) { applyConfig(cfg); }

// ヒープ (mbedTLS が使う) の空き: sbrk の残り + 解放済みのブロック
uint32_t heapFree() {
  const struct mallinfo mi = mallinfo();
  return (uint32_t)(&__StackLimit - (char *)sbrk(0)) + (uint32_t)mi.fordblks;
}

void logMemory() {
  std::printf("memory: app %u, history %u, textures %u, 3D arena %u, 2D arenas %u, pipeline %u, core1 stack %u; "
              "heap free %lu\n",
              (unsigned)sizeof(tkapp::App), (unsigned)sizeof g_history, (unsigned)sizeof g_texture,
              (unsigned)sizeof g_arena3d, (unsigned)sizeof g_gfxArena, (unsigned)pipelineMemoryBytes(),
              (unsigned)sizeof g_core1Stack, (unsigned long)heapFree());
}

struct Stats {
  int frames = 0;
  uint64_t pollUs = 0, advanceUs = 0, sceneUs = 0, draw0Us = 0, draw1Us = 0, joinUs = 0, drainUs = 0;
  uint64_t since = 0;
};

void logStats(Stats &s, uint64_t now) {
  if (s.frames == 0) return;
  const int n = s.frames;
  const int fps10 = (int)(n * 1e7 / (double)(now - s.since) + 0.5);
  const tkapp::Stats &as = g_app.stats();
  const PipelineStatus &ps = pipelineStatus();
  std::printf("%d.%d fps, us/frame: poll %llu, advance %llu, scene %llu, draw core0 %llu core1 %llu, "
              "wait core1 %llu, wait panel %llu; tris %u, span peak %u, tri bytes %u; heap free %lu; "
              "fetch %lu ok %lu same %lu err %lu, last %lu ms\n",
              fps10 / 10, fps10 % 10, s.pollUs / n, s.advanceUs / n, s.sceneUs / n, s.draw0Us / n, s.draw1Us / n,
              s.joinUs / n, s.drainUs / n, (unsigned)as.triCount, (unsigned)as.spanPeak, (unsigned)as.triBytes,
              (unsigned long)heapFree(), (unsigned long)ps.fetches, (unsigned long)ps.ok,
              (unsigned long)ps.unchanged, (unsigned long)ps.errors, (unsigned long)ps.lastFetchMs);
  s = Stats();
  s.since = now;
}

[[noreturn]] void halt(const char *msg) {
  for (;;) {
    std::printf("%s\n", msg);
    sleep_ms(1000);
  }
}

}  // namespace

rp2common::SpiPanel &panel() { return g_panel; }

void run(Board &board, const Config &cfg) {
  const bool clockOk = rp2common::initClocks(rp2common::defaultClockConfig());
  stdio_init_all();
  g_demo = cfg.demo;
  g_platform.demo_ = cfg.demo;

  rp2common::PanelConfig pc = {};
  pc.spi = SPI_INSTANCE(RP2COMMON_LCD_SPI);
  pc.sckPin = RP2COMMON_LCD_SCK_PIN;
  pc.mosiPin = RP2COMMON_LCD_MOSI_PIN;
  pc.csPin = RP2COMMON_LCD_CS_PIN;
  pc.dcPin = RP2COMMON_LCD_DC_PIN;
  pc.rstPin = RP2COMMON_LCD_RST_PIN;
  pc.blPin = RP2COMMON_LCD_BL_PIN;
  pc.nativeW = RP2COMMON_LCD_WIDTH;
  pc.nativeH = RP2COMMON_LCD_HEIGHT;
  pc.frameW = RP2COMMON_LCD_WIDTH;
  pc.frameH = RP2COMMON_LCD_HEIGHT;
  pc.baud = RP2COMMON_LCD_BAUD;
  pc.cmdBaud = RP2COMMON_LCD_CMD_BAUD;
  pc.kind = rp2common::PanelKind::RP2COMMON_LCD_KIND;
  pc.madctl = RP2COMMON_LCD_MADCTL;
  pc.flip = RP2COMMON_LCD_FLIP != 0;
  pc.backlight = RP2COMMON_BACKLIGHT;
  g_w = pc.frameW;
  g_h = pc.frameH;
  const int stripH = std::min(cfg.stripH, g_h);
  const bool panelOk = g_panel.init(pc, stripH);
  if (panelOk) g_panel.setBacklight(pc.backlight);
  for (int i = 0; i < 2; i++) g_gfx[i].init(g_gfxArena[i], GFX2D_ARENA_BYTES);

  board.initDevices();

  // USB のシリアルが列挙されるのを少し待ってからログを出す
  sleep_ms(1000);
  const rp2common::ClockReport &cr = rp2common::clockReport();
  std::printf(TKAPP_DISP_NAME " on %s: frame %dx%d, strips of %d rows, %s%s\n", cfg.name, g_w, g_h, stripH,
              RENDER_CORES > 1 ? "two cores" : "one core", cfg.demo ? ", demo" : "");
  std::printf("clk_sys %lu Hz%s, clk_peri %lu Hz, SPI %lu Hz\n", (unsigned long)cr.sysHz,
              clockOk ? "" : " (not as asked)", (unsigned long)cr.periHz, (unsigned long)g_panel.baud());
  {
    char flash[128];
    rp2common::FlashReport fr = rp2common::flashReport();
    // flashReport() の flash_do_cmd() が XIP を起動経路の設定に戻しているので、ここで揃える
    rp2common::configureXip(fr);
    fr.rfmt = qmi_hw->m[0].rfmt;
    fr.rcmd = qmi_hw->m[0].rcmd;
    rp2common::formatFlashReport(fr, flash, sizeof flash);
    std::printf("flash: %s, timing 0x%08lx\n", flash, (unsigned long)qmi_hw->m[0].timing);
  }
  rp2common::diagReport();
  board.log();
  if (!panelOk) halt("strip buffers did not fit");

  tkapp::Buffers buffers;
  buffers.history = g_history;
  buffers.historyCapacity = tkapp::HISTORY_MAX;
  buffers.texturePixels = g_texture;
  g_app.setMultiTouch(cfg.multiTouch);
  if (!g_app.init(g_w, g_h, buffers, &g_platform, g_arena3d, ARENA_BYTES, RENDER_CORES)) halt("app init failed");
  if (settingsLoad(g_wifi)) g_app.setWifiConfig(g_wifi);

  // core1: WiFi を初期化してから帯の描画を待つ
  multicore_launch_core1_with_stack(core1Main, g_core1Stack, sizeof(g_core1Stack));
  const uint64_t waitStart = time_us_64();
  while (!g_core1Ready && time_us_64() - waitStart < CORE1_READY_TIMEOUT_US) tight_loop_contents();
  __dmb();
  if (!g_core1Ready) halt("core1 did not start (WiFi init hung)");
  if (!cfg.demo) {
    std::printf("wifi: %s\n", g_netOk ? "CYW43 up" : "not available");
    if (g_netOk) {
      if (g_wifi.ssid[0]) netSetConfig(g_wifi);
      else std::printf("wifi: no credentials. type 'wifi' on the serial console or tap the icon\n");
    }
  }
  PipelineConfig plc;
  plc.demo = cfg.demo;
  plc.intervalSec = cfg.demo ? cfg.demoIntervalSec : tkapp::FETCH_INTERVAL_SEC;
  pipelineStart(plc);
  ConsoleHooks hooks;
  hooks.apply = applyConfig;
  hooks.current = []() -> const tkapp::WifiConfig & { return g_wifi; };
  hooks.status = logMemory;
  consoleStart(hooks);
  logMemory();

  const uint64_t start = time_us_64();
  uint64_t lastT = start;
  Stats stats;
  stats.since = start;
  int cur = 0;
  uint32_t logGen = 0;
  rp2common::diagStartWatchdog();
  for (;;) {
    rp2common::diagFeedWatchdog();
    rp2common::diagMark(0, rp2common::Phase::UPDATE, 0);
    const uint64_t t0 = time_us_64();
    if (!cfg.demo) netPoll(t0);
    pipelinePoll(t0);
    // 表示の時刻が来たイベントを受け取る
    while (tkapp::EventRecord *rec = pipelineTake(t0)) {
      g_app.pushEvent(*rec);
      pipelineRelease(rec);
    }
    // イベントの記録が更新されていたら最下部の画像を作り直す
    pipelineVisitLog(logGen, [](const tkapp::EventLog &log, void *) { g_app.updateLog(log); }, nullptr);
    consolePoll();
    board.poll(g_app);
    const uint64_t tp = time_us_64();
    g_app.advance((float)(t0 - lastT) * 1e-6f);
    lastT = t0;
    const uint64_t ta = time_us_64();
    g_app.buildScene();
    const uint64_t t1 = time_us_64();
    stats.pollUs += tp - t0;
    stats.advanceUs += ta - tp;
    stats.sceneUs += t1 - ta;

    // 帯 idx に描いている間、もう一方の帯は転送中。描き終えてからその転送を待ち、idx を送る。
    // 2 コアのときは帯の上半分を core0、下半分を core1 が同時に描く
    for (int y = 0; y < g_h; y += stripH) {
      const int rows = std::min(stripH, g_h - y);
      const int idx = cur;
      cur ^= 1;
      uint16_t *buf = g_panel.buffer(idx);
      const uint64_t d0 = time_us_64();
      uint64_t d1;
      if (RENDER_CORES > 1) {
        const int upper = rows / 2;
        g_job = {buf + upper * g_w, y + upper, rows - upper};
        __dmb();
        multicore_fifo_push_blocking(CORE1_DRAW);
        rp2common::diagMark(0, rp2common::Phase::DRAW, y);
        renderRows(0, buf, y, upper);
        d1 = time_us_64();
        rp2common::diagMark(0, rp2common::Phase::WAIT_CORE1, y);
        stats.draw1Us += multicore_fifo_pop_blocking();
        __dmb();
      } else {
        rp2common::diagMark(0, rp2common::Phase::DRAW, y);
        renderRows(0, buf, y, rows);
        d1 = time_us_64();
      }
      const uint64_t d2 = time_us_64();
      rp2common::diagMark(0, rp2common::Phase::WAIT_PANEL, y);
      g_panel.drain();
      const uint64_t d3 = time_us_64();
      if (y == 0) {
        // 前のフレームの最後の帯を送り終え、バスが空いている
        board.onPanelIdle();
        stats.pollUs += time_us_64() - d3;
      }
      __dmb();
      g_panel.submit(idx, y, rows);
      stats.draw0Us += d1 - d0;
      stats.joinUs += d2 - d1;
      stats.drainUs += d3 - d2;
    }
    rp2common::diagMark(0, rp2common::Phase::END_FRAME, 0);
    g_app.endFrame();
    if (g_savePending) saveSettings();
    stats.frames++;
    const uint64_t now = time_us_64();
    if (now - stats.since >= LOG_INTERVAL_US) logStats(stats, now);
  }
}

}  // namespace tkrp2
