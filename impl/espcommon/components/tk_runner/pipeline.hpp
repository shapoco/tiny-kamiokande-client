#ifndef TK_PIPELINE_HPP
#define TK_PIPELINE_HPP

// データ取得のパイプライン。core1 のタスクが一定間隔で api.php (またはデモの埋め込みデータ) を
// 読み、展開した EventRecord (推定結果はサーバが応答に入れて返す) をキューに積む。描画側はフレームごとに poll() で受け取り、
// App::pushEvent() でコピーした後 release() で返す。poll() は表示の時刻 (timestampMs + 遅れ、サーバ時刻) が
// 来たものだけを返す (端末間の同期。SPEC.md 5.1)

#include "tkapp/event.hpp"
#include "tkapp/eventlog.hpp"
#include "tkapp/history.hpp"

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

// 履歴の一覧 (api.php の hist) が gen より新しければ、ロックしたまま fn に渡して gen を更新する。描画側が呼ぶ
bool pipelineVisitHistory(uint32_t &gen, void (*fn)(const tkapp::HistoryList &list, void *user), void *user);

// 時刻 ms の履歴の内容 (?at=) の取得を頼む。前の頼みが始まっていなければ置き換える
void pipelineRequestHistory(int64_t ms);
// 履歴の取得の結果があれば true。*rec が nullptr なら失敗。*rec は App に渡した後 pipelineRelease() で返す
bool pipelineTakeHistory(tkapp::EventRecord **rec, int64_t *ms);

// 表示の時刻 (サーバ時刻 - 表示の遅れ、UNIX ms)。分からなければ -1
int64_t pipelineDisplayTimeMs();

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
