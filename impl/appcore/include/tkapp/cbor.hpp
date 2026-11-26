#ifndef TKAPP_CBOR_HPP
#define TKAPP_CBOR_HPP

// api.php?fmt=bin の CBOR 応答 (SPEC.md 2.2) を EventRecord (データとサーバの推定結果) に変換する。
// 鍵が決まっている応答専用の最小のデコーダで、不定長や浮動小数は扱わない

#include <cstddef>
#include <cstdint>

#include "tkapp/event.hpp"
#include "tkapp/history.hpp"

namespace tkapp {

enum class ApiStatus : uint8_t {
  OK,
  MALFORMED,     // CBOR として読めない、または必要な鍵が無い
  SERVER_ERROR,  // statusCode が 0 でない (errorMsg を見る)
  FEW_LIT,       // 点灯が MIN_LIT_PMTS 未満 (空の画像)
};

struct ApiInfo {
  ApiStatus status = ApiStatus::MALFORMED;
  int statusCode = -1;
  char errorMsg[64] = {};
  int64_t serverTimeMs = 0;  // 応答を返したサーバ時刻 (UNIX ミリ秒)。応答に無ければ 0
  int64_t clients = -1;      // 接続数 (直近 20 秒にアクセスした IP アドレスの数)。応答に無ければ -1
  // イベントの記録 (?log= を付けたときの log。EventLog::merge に渡す)。logRows は buf 内を指す。無ければ logNow = -1
  int64_t logNow = -1;
  const uint8_t *logRows = nullptr;
  size_t logBytes = 0;
  // 履歴の一覧 (?hist= を付けたときの history)。時間軸に出す種別だけ
  HistoryChunk history;
};

// buf の応答を rec に展開する。FEW_LIT でも rec は埋まる。SERVER_ERROR / MALFORMED では rec は不定。
// info の記録 (log) は SERVER_ERROR でも埋まる。

// 応答に推定結果 (fit) がなければ rec.fit は「推定できなかった」(show = false, hideReason = HIDE_NO_RESULT)
ApiStatus decodeApiResponse(const uint8_t *buf, size_t len, EventRecord &rec, ApiInfo &info);

// RGB565 の色 → セルの値 (0 は未点灯、1..16 は凡例の色番号 + 1)
Cell cellFromRgb565(uint16_t rgb565);

}  // namespace tkapp

#endif
