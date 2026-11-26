#include "tkapp/history.hpp"

#include "tkapp/config.hpp"

namespace tkapp {

namespace {
// 表示の遅れ (表示の時刻はサーバ時刻より最大 QUEUE_DELAY_SEC 前) の分も含めて、少し余計に持つ
constexpr int64_t KEEP_MS = HISTORY_SPAN_MS + (int64_t)(QUEUE_DELAY_SEC * 1000) + 60 * 1000;
}  // namespace

void HistoryList::merge(const HistoryChunk &chunk, int64_t serverNowMs) {
  if (!chunk.present) return;
  for (int i = 0; i < chunk.count; i++) {
    const HistoryItem &it = chunk.items[i];
    if (count_ > 0 && it.ms <= items_[count_ - 1].ms) continue;  // 古い順に来る。重複は捨てる
    if (count_ == HISTORY_LIST_MAX) {
      for (int k = 1; k < count_; k++) items_[k - 1] = items_[k];
      count_--;
    }
    items_[count_++] = it;
  }
  if (chunk.lastMs > lastMs_) lastMs_ = chunk.lastMs;
  if (serverNowMs > 0) {
    int drop = 0;
    while (drop < count_ && items_[drop].ms < serverNowMs - KEEP_MS) drop++;
    for (int k = drop; k < count_; k++) items_[k - drop] = items_[k];
    count_ -= drop;
  }
}

int64_t HistoryList::requestFrom(int64_t serverNowMs) const {
  if (lastMs_ >= 0) return lastMs_;
  if (serverNowMs <= 0) return -1;
  return serverNowMs - KEEP_MS;
}

}  // namespace tkapp
