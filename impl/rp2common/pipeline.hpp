#ifndef TKRP2_PIPELINE_HPP
#define TKRP2_PIPELINE_HPP

// データ取得のパイプライン (Pico 2 W)。ESP-IDF 版の pipeline.cpp と同じ流れを core0 のフレームループから回す:
// 一定間隔で api.php (またはデモの埋め込みデータ) を読み、展開した EventRecord を表示待ちのキューに積む。
// 通信そのものは core1 の割り込みで進み (net.hpp)、展開 (数 ms) は届いたフレームで core0 が行う。
// pipelineTake() は表示の時刻 (timestampMs + 遅れ、サーバ時刻) が来たものだけを返す (SPEC.md 3.3、5.1)

#include <cstdint>

#include "tkapp/event.hpp"
#include "tkapp/eventlog.hpp"

namespace tkrp2 {

struct PipelineConfig {
  bool demo;
  float intervalSec;
};

void pipelineStart(const PipelineConfig &cfg);
// 毎フレーム呼ぶ。取得を始め、届いたものを展開する
void pipelinePoll(uint64_t nowUs);
// 表示の時刻が来たイベントを取り出す。App::pushEvent() でコピーしたら pipelineRelease() で返す
tkapp::EventRecord *pipelineTake(uint64_t nowUs);
void pipelineRelease(tkapp::EventRecord *rec);

// イベントの記録が gen より新しければ fn に渡して gen を更新する
bool pipelineVisitLog(uint32_t &gen, void (*fn)(const tkapp::EventLog &log, void *user), void *user);

bool pipelineFetching();
// 最後に受け取った接続数。不明か 60 秒以上更新がなければ -1
int pipelineViewerCount(uint64_t nowUs);

struct PipelineStatus {
  uint32_t fetches, ok, unchanged, fewLit, errors, dropped;
  uint32_t lastFetchMs, lastFitMs, lastHandshakeMs, lastBytes;
  uint32_t reused, newConnections;
  int lastHttpStatus;
};
const PipelineStatus &pipelineStatus();

// 静的に確保しているメモリ (ログ用)
uint32_t pipelineMemoryBytes();

}  // namespace tkrp2

#endif
