#ifndef TK_PIPELINE_HPP
#define TK_PIPELINE_HPP

// データ取得のパイプライン。core1 のタスクが一定間隔で api.php (またはデモの埋め込みデータ) を
// 読み、展開した EventRecord (推定結果はサーバが応答に入れて返す) をキューに積む。描画側はフレームごとに poll() で受け取り、
// App::pushEvent() でコピーした後 release() で返す。poll() は表示の時刻 (timestampMs + 遅れ、サーバ時刻) が
// 来たものだけを返す (端末間の同期。SPEC.md 5.1)

#include "tkapp/event.hpp"
#include "tkapp/eventlog.hpp"

namespace tkrunner {

struct PipelineConfig {
  bool demo;
  float intervalSec;
  int core;       // タスクを置くコア
  int priority;
};

bool pipelineStart(const PipelineConfig &cfg);
tkapp::EventRecord *pipelinePoll();
void pipelineRelease(tkapp::EventRecord *rec);

// イベントの記録 (api.php の log) が gen より新しければ、ロックしたまま fn に渡して gen を更新する。描画側が呼ぶ
bool pipelineVisitLog(uint32_t &gen, void (*fn)(const tkapp::EventLog &log, void *user), void *user);

// api.php から取得している最中か (デモでは常に false)

bool pipelineFetching();

// 最後に受け取った接続数 (api.php の clients)。不明か 60 秒以上更新がなければ -1
int pipelineViewerCount();

// 直近の取得の状態 (ログ用)
struct PipelineStatus {
  uint32_t fetches, ok, unchanged, fewLit, errors;
  uint32_t lastFetchMs, lastFitMs;  // lastFitMs はサーバでの推定時間
  int lastHttpStatus;
};
const PipelineStatus &pipelineStatus();

}  // namespace tkrunner

#endif
