#ifndef TKAPP_EVENTLOG_HPP
#define TKAPP_EVENTLOG_HPP

// サーバのイベントの記録 (api.php?log=、SPEC.md 2.4)。10 分枠ごとの取得回数と種別ごとのイベント数を
// 直近 LOG_MAX_COLS 時間分だけ持ち、画面最下部の 1 列 1 時間、1 マス 10 分の画像にする

#include <cstddef>
#include <cstdint>

namespace tkapp {

constexpr int LOG_SLOT_SEC = 600;
constexpr int LOG_SLOTS_PER_HOUR = 6;   // 画像の行数
constexpr int LOG_KINDS = 7;            // エラー、発光不足、推定不能、貫通、停止、発生、発生・停止
constexpr int LOG_ROW_BYTES = 4 + 2 * (1 + LOG_KINDS);  // 応答の 1 行 (u32 枠の番号, u16 取得回数, u16 × 7)
constexpr int LOG_MAX_COLS = 80;        // 画像の列数 (時間数) の上限。CoreS3 は 80、Tab5 は 72
constexpr int LOG_TZ_HOURS = 9;         // 区切り線を引く時刻帯 (日本時間)。端末は時刻帯を知らないので固定
constexpr int LOG_GRID_HOURS = 6;       // この時間ごと (現地時刻の 0, 6, 12, 18 時) に区切り線を引く


enum LogKind : uint8_t {
  LOG_ERROR = 0, LOG_DIM, LOG_UNFIT, LOG_THROUGH, LOG_STOPPING, LOG_INSIDE, LOG_CONTAINED,
};

struct LogCounts {
  uint16_t fetches;         // 元画像の取得回数 (0 なら誰もアクセスしなかった)
  uint16_t n[LOG_KINDS];    // 種別ごとのイベント数
};

class EventLog {
 public:
  static constexpr int CAPACITY = LOG_MAX_COLS * LOG_SLOTS_PER_HOUR;

  void clear();
  // 応答の log を取り込む。rows は LOG_ROW_BYTES バイトの行の連結。古すぎる行は捨てる
  void merge(int64_t nowSlot, const uint8_t *rows, size_t bytes);
  // 次の要求の log= の値。初回は直近 CAPACITY 枠、以降は前回の現在の枠 (集計中) から
  int64_t requestFrom() const { return now_ < 0 ? -(int64_t)CAPACITY : now_; }
  int64_t nowSlot() const { return now_; }
  const LogCounts *find(int64_t slot) const;

 private:
  int64_t now_ = -1;
  uint32_t slot_[CAPACITY] = {};  // その行の枠の番号 (0 は空)
  LogCounts counts_[CAPACITY] = {};
};

// 1 マスの色 (ARGB8888)。counts が null なら記録なし
uint32_t logCellColor(const LogCounts *counts);

// cols 列 × LOG_SLOTS_PER_HOUR 行の画像 (RGB565_SWAPPED、行優先) を作る。右端の列が現在の時 (UTC の時。
// 日本時間とは時の区切りが同じ)、上から 10 分ずつ。まだ来ていない枠と記録のない枠は黒
void buildLogImage(const EventLog &log, int cols, uint16_t *pixels);

}  // namespace tkapp

#endif
