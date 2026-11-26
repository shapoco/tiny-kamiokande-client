#ifndef TKAPP_HISTORY_HPP
#define TKAPP_HISTORY_HPP

// サーバの履歴 (api.php の ?hist= の一覧と ?at= の内容、SPEC.md 2.6) のうち、画面下部の時間軸に出すもの。
// 画面が狭いので、時間軸は直近 1 時間で、壁から入って停止・検出器内で発生・発生して停止だけを出す (他は捨てる)

#include <cstdint>

#include "tkapp/eventlog.hpp"

namespace tkapp {

constexpr int64_t HISTORY_SPAN_MS = 3600 * 1000;  // 時間軸の幅 (表示の時刻から過去へ)
constexpr int HISTORY_LIST_MAX = 64;              // 時間軸に持つ件数の上限 (実際は 1 時間に 2〜6 件程度)
constexpr int HISTORY_CHUNK_MAX = 32;             // 1 回の応答から取り込む件数の上限 (新しい方を残す)

// 時間軸に出す種別か (種別の番号はイベントの記録と同じ。eventlog.hpp の LogKind)
inline bool historyKindShown(int kind) { return kind == LOG_STOPPING || kind == LOG_INSIDE || kind == LOG_CONTAINED; }

struct HistoryItem {
  int64_t ms;    // 内容がサーバに最初に現れた時刻 (UNIX ミリ秒。?at= に渡す値)
  uint8_t kind;  // LogKind
};

// 1 回の応答の history (cbor.cpp が ApiInfo に入れる)。時間軸に出す種別だけ
struct HistoryChunk {
  bool present = false;  // 応答に history があった
  int64_t lastMs = -1;   // 種別を問わず最後の時刻 (次の要求の hist= に使う)
  int count = 0;
  HistoryItem items[HISTORY_CHUNK_MAX];
};

// 直近の一覧。パイプラインが応答から取り込み、描画側が App::updateHistory() で写す
class HistoryList {
 public:
  // 取り込む。serverNowMs (サーバ時刻、分からなければ 0 以下) より HISTORY_SPAN_MS 以上古いものは捨てる
  void merge(const HistoryChunk &chunk, int64_t serverNowMs);
  // 次の要求の hist= の値。初回はサーバ時刻の 1 時間 (と少し) 前から。サーバ時刻が分からなければ -1 (付けない)
  int64_t requestFrom(int64_t serverNowMs) const;
  int count() const { return count_; }
  const HistoryItem &at(int i) const { return items_[i]; }

 private:
  HistoryItem items_[HISTORY_LIST_MAX] = {};  // 古い順
  int count_ = 0;
  int64_t lastMs_ = -1;
};

}  // namespace tkapp

#endif
