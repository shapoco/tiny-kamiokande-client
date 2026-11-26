#include "pipeline.hpp"

#include <cstdio>
#include <cstring>

#include "net.hpp"
#include "tkapp/cbor.hpp"
#include "tkapp/config.hpp"
#include "tkapp/sync.hpp"

// samples.S が埋め込むデモ用のデータ (impl/espcommon/samples/*.cbor)
extern "C" const uint8_t tk_s1_start[], tk_s1_end[], tk_s2_start[], tk_s2_end[], tk_s3_start[], tk_s3_end[];

namespace tkrp2 {

namespace {

// 表示待ち (15 秒 / 9 秒 → 最大 2 件) + 余裕 1 件。展開は届いたフレームで済むので、取得中の分は要らない。
// 足りなければ一番古い待ちを捨てる (ログに出す)
constexpr int SLOTS = 3;
// api.php?fmt=bin&log=... は約 26 KB + 記録 (初回は最大 9.6 KB) + 外水槽のヒット (1 件 6 バイト)
constexpr size_t FETCH_BUF_BYTES = 48 * 1024;
constexpr size_t URL_MAX = 160;
constexpr uint64_t VIEWERS_STALE_US = 60 * 1000000ull;

PipelineConfig g_cfg;
tkapp::EventRecord g_slots[SLOTS];
bool g_used[SLOTS];             // 待ちか、描画側が持っている
tkapp::EventRecord *g_ready[SLOTS];  // 表示待ち (古い順)
int g_readyCount = 0;
alignas(4) uint8_t g_fetchBuf[FETCH_BUF_BYTES];
tkapp::EventLog g_log;
uint32_t g_logGen = 0;
tkapp::HistoryList g_hist;
uint32_t g_histGen = 0;
int64_t g_histReqMs = 0;      // 頼まれた履歴 (0 なら無し)
int64_t g_histFetchMs = 0;    // 取得中の履歴 (0 なら定期の取得)
bool g_histDone = false;      // 結果を pipelineTakeHistory() で渡す
tkapp::EventRecord *g_histRec = nullptr;  // nullptr なら失敗
int64_t g_histResultMs = 0;
tkapp::DisplaySync g_sync;
PipelineStatus g_status = {};
bool g_fetching = false;
uint64_t g_nextFetchUs = 0, g_fetchStartUs = 0;
uint32_t g_lastCrc = 0;
int g_sampleIndex = 0;
int g_viewers = -1;
uint64_t g_viewersAtUs = 0;

struct Sample {
  const uint8_t *start, *end;
};
const Sample SAMPLES[] = {{tk_s1_start, tk_s1_end}, {tk_s2_start, tk_s2_end}, {tk_s3_start, tk_s3_end}};
constexpr int SAMPLE_COUNT = (int)(sizeof(SAMPLES) / sizeof(SAMPLES[0]));

tkapp::EventRecord *allocSlot() {
  for (int i = 0; i < SLOTS; i++) {
    if (!g_used[i]) {
      g_used[i] = true;
      return &g_slots[i];
    }
  }
  // 全部待ちなら一番古いものを捨てる (描画側が持っている分は待ちに入っていない)
  if (g_readyCount == 0) return nullptr;
  tkapp::EventRecord *rec = g_ready[0];
  for (int i = 1; i < g_readyCount; i++) g_ready[i - 1] = g_ready[i];
  g_readyCount--;
  g_status.dropped++;
  std::printf("pipeline: queue full, dropped the oldest event\n");
  return rec;
}

void freeSlot(tkapp::EventRecord *rec) {
  for (int i = 0; i < SLOTS; i++)
    if (&g_slots[i] == rec) g_used[i] = false;
}

// 応答を展開してキューに積む
void accept(const uint8_t *data, size_t bytes, const HttpResult *http, uint64_t nowUs) {
  tkapp::EventRecord *rec = allocSlot();
  if (!rec) return;
  tkapp::ApiInfo info;
  const tkapp::ApiStatus st = tkapp::decodeApiResponse(data, bytes, *rec, info);
  if (http) {
    g_sync.noteServerTime(info.serverTimeMs, http->sentUs, http->headersUs);
    if (st != tkapp::ApiStatus::MALFORMED && info.history.present) {
      g_hist.merge(info.history, g_sync.serverNowMs((int64_t)nowUs));
      g_histGen++;
    }
    if (st != tkapp::ApiStatus::MALFORMED && info.logNow > 0) {
      g_log.merge(info.logNow, info.logRows, info.logBytes);
      g_logGen++;
    }
    if (info.clients >= 0) {
      g_viewers = (int)info.clients;
      g_viewersAtUs = nowUs;
    }
  }
  bool keep = false;
  if (st == tkapp::ApiStatus::OK) {
    if (http && rec->data.crc32 == g_lastCrc) {
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
    freeSlot(rec);
    return;
  }
  g_status.ok++;
  g_status.lastFitMs = rec->fit.fitMs;
  g_sync.startRamp((int64_t)nowUs);
  g_ready[g_readyCount++] = rec;
}

}  // namespace

void pipelineStart(const PipelineConfig &cfg) {
  g_cfg = cfg;
  g_nextFetchUs = 0;
}

void pipelinePoll(uint64_t nowUs) {
  if (g_fetching) {
    const HttpState st = httpPoll(nowUs);
    if (st == HttpState::BUSY) return;
    const HttpResult &r = httpResult();
    g_fetching = false;
    g_status.lastFetchMs = (uint32_t)((nowUs - g_fetchStartUs) / 1000);
    g_status.lastHttpStatus = r.status;
    g_status.lastBytes = (uint32_t)r.bytes;
    if (r.reused) g_status.reused++;
    else {
      g_status.newConnections++;
      g_status.lastHandshakeMs = r.handshakeUs / 1000;
    }
    std::printf("%s: http %d, %u bytes, %lu ms, %s (connect %lu ms)\n", g_histFetchMs ? "history" : "fetch",
                r.status, (unsigned)r.bytes,
                (unsigned long)g_status.lastFetchMs, r.reused ? "reused" : "new connection",
                (unsigned long)(r.handshakeUs / 1000));
    if (g_histFetchMs) {
      // 履歴の内容: 展開して描画側に渡す (失敗も渡す)
      const int64_t ms = g_histFetchMs;
      g_histFetchMs = 0;
      const bool done = st == HttpState::DONE && r.bytes > 0;
      httpFinish();
      tkapp::EventRecord *rec = done ? allocSlot() : nullptr;
      if (rec) {
        tkapp::ApiInfo info;
        const tkapp::ApiStatus ds = tkapp::decodeApiResponse(g_fetchBuf, r.bytes, *rec, info);
        if (ds != tkapp::ApiStatus::OK && ds != tkapp::ApiStatus::FEW_LIT) {
          std::printf("pipeline: history %lld not available (status %d, code %d)\n", (long long)ms, (int)ds,
                      info.statusCode);
          freeSlot(rec);
          rec = nullptr;
        }
      }
      if (g_histDone && g_histRec) freeSlot(g_histRec);  // 受け取られていない前の結果は捨てる
      g_histRec = rec;
      g_histResultMs = ms;
      g_histDone = true;
    } else if (st == HttpState::DONE && r.bytes > 0) {
      const HttpResult copy = r;
      httpFinish();
      accept(g_fetchBuf, copy.bytes, &copy, nowUs);
    } else {
      httpFinish();
      g_status.errors++;
      std::printf("pipeline: fetch failed (http %d)\n", r.status);
    }
    return;
  }
  if (g_histReqMs) {
    const int64_t ms = g_histReqMs;
    g_histReqMs = 0;
    char url[URL_MAX];
    std::snprintf(url, sizeof url, "%s&at=%lld", TKAPP_API_URL, (long long)ms);
    if (!g_cfg.demo && netState() == tkapp::WifiState::CONNECTED && httpStart(url, g_fetchBuf, sizeof g_fetchBuf, nowUs)) {
      g_histFetchMs = ms;
      g_fetchStartUs = nowUs;
      g_fetching = true;
    } else {
      if (g_histDone && g_histRec) freeSlot(g_histRec);
      g_histRec = nullptr;
      g_histResultMs = ms;
      g_histDone = true;
    }
    return;
  }
  if (nowUs < g_nextFetchUs) return;
  if (g_cfg.demo) {
    g_nextFetchUs = nowUs + (uint64_t)(g_cfg.intervalSec * 1e6f);
    g_status.fetches++;
    const Sample &s = SAMPLES[g_sampleIndex];
    g_sampleIndex = (g_sampleIndex + 1) % SAMPLE_COUNT;
    accept(s.start, (size_t)(s.end - s.start), nullptr, nowUs);
    return;
  }
  if (netState() != tkapp::WifiState::CONNECTED) return;
  // 取得の開始から次の開始まで FETCH_INTERVAL_SEC。記録は前回の応答の現在の枠から後ろだけを要求する
  char url[URL_MAX];
  // 履歴の一覧も前回の最後の時刻から後ろだけ (初回はサーバ時刻が分かってから、直近 1 時間分)
  const int64_t histFrom = g_hist.requestFrom(g_sync.serverNowMs((int64_t)nowUs));
  if (histFrom >= 0) {
    std::snprintf(url, sizeof url, "%s&log=%lld&hist=%lld", TKAPP_API_URL, (long long)g_log.requestFrom(),
                  (long long)histFrom);
  } else {
    std::snprintf(url, sizeof url, "%s&log=%lld", TKAPP_API_URL, (long long)g_log.requestFrom());
  }
  if (!httpStart(url, g_fetchBuf, sizeof g_fetchBuf, nowUs)) return;
  g_nextFetchUs = nowUs + (uint64_t)(g_cfg.intervalSec * 1e6f);
  g_fetchStartUs = nowUs;
  g_fetching = true;
  g_status.fetches++;
}

tkapp::EventRecord *pipelineTake(uint64_t nowUs) {
  if (g_readyCount == 0) return nullptr;
  tkapp::EventRecord *rec = g_ready[0];
  if (!g_cfg.demo && !g_sync.isDue(rec->data.timestampMs, (int64_t)nowUs)) return nullptr;
  for (int i = 1; i < g_readyCount; i++) g_ready[i - 1] = g_ready[i];
  g_readyCount--;
  return rec;
}

void pipelineRelease(tkapp::EventRecord *rec) { freeSlot(rec); }

bool pipelineVisitLog(uint32_t &gen, void (*fn)(const tkapp::EventLog &log, void *user), void *user) {
  if (gen == g_logGen) return false;
  gen = g_logGen;
  fn(g_log, user);
  return true;
}

bool pipelineVisitHistory(uint32_t &gen, void (*fn)(const tkapp::HistoryList &list, void *user), void *user) {
  if (gen == g_histGen) return false;
  gen = g_histGen;
  fn(g_hist, user);
  return true;
}

void pipelineRequestHistory(int64_t ms) { g_histReqMs = ms; }

bool pipelineTakeHistory(tkapp::EventRecord **rec, int64_t *ms) {
  if (!g_histDone) return false;
  g_histDone = false;
  *rec = g_histRec;
  *ms = g_histResultMs;
  g_histRec = nullptr;
  return true;
}

int64_t pipelineDisplayTimeMs(uint64_t nowUs) {
  return g_cfg.demo ? -1 : g_sync.displayTimeMs((int64_t)nowUs);
}

bool pipelineFetching() { return g_fetching; }

int pipelineViewerCount(uint64_t nowUs) {
  if (g_viewers < 0 || nowUs - g_viewersAtUs > VIEWERS_STALE_US) return -1;
  return g_viewers;
}

const PipelineStatus &pipelineStatus() { return g_status; }

uint32_t pipelineMemoryBytes() { return (uint32_t)(sizeof g_slots + sizeof g_fetchBuf + sizeof g_log + sizeof g_hist); }

}  // namespace tkrp2
