#ifndef TKAPP_SYNC_HPP
#define TKAPP_SYNC_HPP

// 表示待ちのキューの時刻の判定 (端末間の同期。SPEC.md 3.3、5.1)。
// イベントは timestampMs (内容がサーバに最初に現れた時刻) + 遅れ (サーバ時刻) が来たら表示する。
// 遅れは最初のイベントを受け取った時点で 0、QUEUE_RAMP_SEC かけて QUEUE_DELAY_SEC まで伸ばす。
// 時刻はすべて端末の単調な時計 (us) で渡す。スレッドの排他は呼び出し側が行う

#include <cstdint>

namespace tkapp {

class DisplaySync {
 public:
  // 取得 1 回分の標本。sentUs は要求を送り終えた時刻、headersUs は応答のヘッダが届いた時刻。
  // サーバは応答を書き出す直前に serverTimeMs を記録するので、ヘッダが届いた時刻と比べる
  // (誤差は下りの片道程度)。直近 SAMPLES 回で「送ってからヘッダが届くまで」が最も短いものを使う
  void noteServerTime(int64_t serverTimeMs, int64_t sentUs, int64_t headersUs);

  // 最初のイベントを受け取ったら呼ぶ (遅れのランプを始める。2 回目以降は何もしない)
  void startRamp(int64_t nowUs) {
    if (rampStartUs_ < 0) rampStartUs_ = nowUs;
  }

  // timestampMs のイベントを表示してよいか。サーバ時刻が分からない、時刻が無い、
  // 異常に先の時刻 (遅れの 2 倍以上) なら待たない
  bool isDue(int64_t timestampMs, int64_t nowUs) const;

  bool clockValid() const { return valid_; }
  int64_t clockOffsetMs() const { return offsetMs_; }  // サーバ時刻 (UNIX ms) - 端末の時計 (ms)

 private:
  static constexpr int SAMPLES = 10;
  struct Sample {
    int64_t waitUs, offsetMs;
  };
  Sample samples_[SAMPLES] = {};
  int count_ = 0, next_ = 0;
  bool valid_ = false;
  int64_t offsetMs_ = 0;
  int64_t rampStartUs_ = -1;  // -1 なら未開始
};

}  // namespace tkapp

#endif
