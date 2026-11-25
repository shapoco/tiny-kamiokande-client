#ifndef TKAPP_EVENT_HPP
#define TKAPP_EVENT_HPP

// イベント 1 件のデータ (api.php の応答を要約したもの) とサーバの推定結果

#include <cstddef>
#include <cstdint>

#include "tkapp/config.hpp"

namespace tkapp {

// 内水槽の PMT の値。0 は未点灯、1..16 は凡例の色番号 + 1。
// 並びは側面、上面、底面の順、各面は index = x * height + y (api.php の pixels と同じ)
using Cell = uint8_t;
constexpr int CELL_PALETTE_COUNT = 16;

struct EventData {
  uint32_t timestamp;    // このピクセル内容がサーバに最初に現れた UNIX 秒
  int64_t timestampMs;   // 同じ時刻の UNIX ミリ秒 (応答に無ければ timestamp × 1000)
  uint32_t crc32;      // pixelsCrc32
  uint16_t litCount;   // 色の付いた PMT の数
  uint16_t odCount;    // 外水槽のヒットの数 (ログ用。MCU 版は外水槽を描かない)
  Cell cells[N_PMTS];
};

enum class Kind : uint8_t { THROUGH = 0, STOPPING = 1, INSIDE = 2, CONTAINED = 3 };

// api.php の応答の fit (SPEC.md 2.3)。座標は PMT ピッチ単位
struct FitResult {
  bool done = false;      // 結果が入っている (false なら空のスロット)
  bool show = false;      // 軌跡を表示する (コアの hide_reason が NONE)
  Kind kind = Kind::THROUGH;
  uint8_t hideReason = 0;  // HIDE_*
  float start[3], entry[3], end[3], exit[3];
  float corr = 0;
  uint32_t fitMs = 0;      // サーバで推定にかかった時間
};

// 履歴の 1 件。実体は大きいのでプラットフォームが置き場所 (PSRAM など) を決める
struct EventRecord {
  EventData data;
  FitResult fit;
};

inline bool isInsideKind(Kind k) { return k == Kind::INSIDE || k == Kind::CONTAINED; }


}  // namespace tkapp

#endif
