#include "tkapp/cbor.hpp"

#include <cstring>

#include "tkapp/eventlog.hpp"


namespace tkapp {

namespace {

struct Reader {
  const uint8_t *p;
  const uint8_t *end;
  bool ok = true;

  bool need(size_t n) {
    if ((size_t)(end - p) < n) ok = false;
    return ok;
  }
  // 項目の先頭。major type と引数 (長さまたは値) を返す
  bool head(int &major, uint64_t &arg) {
    if (!need(1)) return false;
    const uint8_t ib = *p++;
    major = ib >> 5;
    const int ai = ib & 31;
    if (ai < 24) {
      arg = ai;
    } else if (ai == 24) {
      if (!need(1)) return false;
      arg = *p++;
    } else if (ai == 25) {
      if (!need(2)) return false;
      arg = ((uint64_t)p[0] << 8) | p[1];
      p += 2;
    } else if (ai == 26) {
      if (!need(4)) return false;
      arg = ((uint64_t)p[0] << 24) | ((uint64_t)p[1] << 16) | ((uint64_t)p[2] << 8) | p[3];
      p += 4;
    } else if (ai == 27) {
      if (!need(8)) return false;
      arg = 0;
      for (int i = 0; i < 8; i++) arg = (arg << 8) | p[i];
      p += 8;
    } else {
      ok = false;  // 不定長は扱わない
      return false;
    }
    return true;
  }
  // 整数 (負は -1 - n)
  bool readInt(int64_t &v) {
    int major;
    uint64_t arg;
    if (!head(major, arg)) return false;
    if (major == 0) v = (int64_t)arg;
    else if (major == 1) v = -1 - (int64_t)arg;
    else ok = false;
    return ok;
  }
  // バイト列または文字列。中身は buf 内を指す
  bool readBytes(int wantMajor, const uint8_t *&data, size_t &len) {
    int major;
    uint64_t arg;
    if (!head(major, arg)) return false;
    if (major != wantMajor || !need((size_t)arg)) {
      ok = false;
      return false;
    }
    data = p;
    len = (size_t)arg;
    p += len;
    return true;
  }
  bool readArrayHead(size_t &count) {
    int major;
    uint64_t arg;
    if (!head(major, arg)) return false;
    if (major != 4) ok = false;
    count = (size_t)arg;
    return ok;
  }
  // 次の項目の major type (読み進めない)。読めなければ -1
  int peekMajor() const { return p < end ? (*p >> 5) : -1; }
  bool readMapHead(size_t &count) {
    int major;
    uint64_t arg;
    if (!head(major, arg)) return false;
    if (major != 5) ok = false;
    count = (size_t)arg;
    return ok;
  }
  // 1 項目を読み飛ばす (入れ子も)
  void skip() {
    int major;
    uint64_t arg;
    if (!head(major, arg)) return;
    switch (major) {
      case 0: case 1: case 7: break;
      case 2: case 3:
        if (need((size_t)arg)) p += arg;
        break;
      case 4:
        for (uint64_t i = 0; i < arg && ok; i++) skip();
        break;
      case 5:
        for (uint64_t i = 0; i < arg && ok; i++) { skip(); skip(); }
        break;
      default: ok = false;
    }
  }
  bool keyIs(const uint8_t *k, size_t n, const char *name) {
    return n == strlen(name) && memcmp(k, name, n) == 0;
  }
};

// セルの値を決める凡例の色 (core の PALETTE_RGB と同じ)
const uint8_t PALETTE_RGB[CELL_PALETTE_COUNT][3] = {
    {153, 0, 204}, {255, 0, 255}, {0, 0, 255},     {0, 51, 255},    {51, 153, 255}, {0, 153, 153},
    {0, 255, 255}, {102, 255, 204}, {0, 255, 0},   {102, 255, 51},  {204, 255, 153}, {255, 255, 0},
    {255, 204, 102}, {255, 102, 51}, {255, 51, 51}, {255, 0, 0},
};

// 面の pixels (RGB565 リトルエンディアン) をセルに変換する。点灯数を返す
int convertPlane(const uint8_t *px, size_t bytes, int w, int h, Cell *dst) {
  const int n = w * h;
  if (bytes < (size_t)n * 2) return -1;
  int lit = 0;
  for (int i = 0; i < n; i++) {
    const uint16_t v = (uint16_t)(px[2 * i] | (px[2 * i + 1] << 8));
    const Cell c = cellFromRgb565(v);
    dst[i] = c;
    if (c) lit++;
  }
  return lit;
}

struct PlaneRef {
  const uint8_t *pixels = nullptr;
  size_t bytes = 0;
  int w = 0, h = 0;
};

bool readPlane(Reader &r, PlaneRef &out) {
  size_t n;
  if (!r.readMapHead(n)) return false;
  for (size_t i = 0; i < n && r.ok; i++) {
    const uint8_t *k;
    size_t kl;
    if (!r.readBytes(3, k, kl)) return false;
    int64_t v;
    if (r.keyIs(k, kl, "width")) { if (r.readInt(v)) out.w = (int)v; }
    else if (r.keyIs(k, kl, "height")) { if (r.readInt(v)) out.h = (int)v; }
    else if (r.keyIs(k, kl, "pixels")) r.readBytes(2, out.pixels, out.bytes);
    else r.skip();
  }
  return r.ok;
}

struct OdRef {
  const uint8_t *hits = nullptr;
  size_t bytes = 0;
  int count = 0;
};

bool readOd(Reader &r, OdRef &out) {
  size_t n;
  if (!r.readMapHead(n)) return false;
  for (size_t i = 0; i < n && r.ok; i++) {
    const uint8_t *k;
    size_t kl;
    if (!r.readBytes(3, k, kl)) return false;
    int64_t v;
    if (r.keyIs(k, kl, "count")) { if (r.readInt(v)) out.count = (int)v; }
    else if (r.keyIs(k, kl, "hits")) r.readBytes(2, out.hits, out.bytes);
    else r.skip();
  }
  return r.ok;
}

// 3 面の map。faces[0] = top, [1] = side, [2] = bottom
template <typename T, typename F>
bool readFaces(Reader &r, T *faces, F readOne) {
  size_t n;
  if (!r.readMapHead(n)) return false;
  for (size_t i = 0; i < n && r.ok; i++) {
    const uint8_t *k;
    size_t kl;
    if (!r.readBytes(3, k, kl)) return false;
    if (r.keyIs(k, kl, "top")) readOne(r, faces[0]);
    else if (r.keyIs(k, kl, "side")) readOne(r, faces[1]);
    else if (r.keyIs(k, kl, "bottom")) readOne(r, faces[2]);
    else r.skip();
  }
  return r.ok;
}

// 推定結果 (api.php の compactFit の形)。null (サーバが推定できなかった) なら out は「結果なし」のまま
bool readFit(Reader &r, FitResult &out) {
  if (r.peekMajor() != 5) {
    r.skip();
    return r.ok;
  }
  size_t n;
  if (!r.readMapHead(n)) return false;
  int64_t hide = HIDE_NO_RESULT, kind = 0, corr = 0, fitMs = 0;
  int64_t track[12] = {};
  bool haveTrack = false;
  for (size_t i = 0; i < n && r.ok; i++) {
    const uint8_t *k;
    size_t kl;
    if (!r.readBytes(3, k, kl)) return false;
    if (r.keyIs(k, kl, "hide")) r.readInt(hide);
    else if (r.keyIs(k, kl, "kind")) r.readInt(kind);
    else if (r.keyIs(k, kl, "corr")) r.readInt(corr);
    else if (r.keyIs(k, kl, "fitMs")) r.readInt(fitMs);
    else if (r.keyIs(k, kl, "track")) {
      size_t m;
      if (!r.readArrayHead(m)) return false;
      for (size_t j = 0; j < m && r.ok; j++) {
        int64_t v;
        if (r.readInt(v) && j < 12) track[j] = v;
      }
      haveTrack = m == 12;
    } else {
      r.skip();
    }
  }
  if (!r.ok) return false;
  out.hideReason = (uint8_t)(hide >= 0 && hide <= 255 ? hide : (int64_t)HIDE_NO_RESULT);
  out.show = haveTrack && out.hideReason == HIDE_NONE;
  out.kind = (Kind)(kind >= 0 && kind <= 3 ? kind : 0);
  out.corr = corr / 1000.0f;
  out.fitMs = (uint32_t)(fitMs > 0 ? fitMs : 0);
  float *dst[4] = {out.start, out.entry, out.end, out.exit};
  for (int j = 0; j < 12; j++) dst[j / 3][j % 3] = track[j] / 65536.0f;
  return true;
}

// イベントの記録 { now, slotSec, rows (バイト列) }。null なら何もしない
bool readLog(Reader &r, ApiInfo &info) {
  if (r.peekMajor() != 5) {
    r.skip();
    return r.ok;
  }
  size_t n;
  if (!r.readMapHead(n)) return false;
  int64_t now = -1, slotSec = 0;
  const uint8_t *rows = nullptr;
  size_t bytes = 0;
  for (size_t i = 0; i < n && r.ok; i++) {
    const uint8_t *k;
    size_t kl;
    if (!r.readBytes(3, k, kl)) return false;
    if (r.keyIs(k, kl, "now")) r.readInt(now);
    else if (r.keyIs(k, kl, "slotSec")) r.readInt(slotSec);
    else if (r.keyIs(k, kl, "rows")) r.readBytes(2, rows, bytes);
    else r.skip();
  }
  if (!r.ok) return false;
  if (slotSec == LOG_SLOT_SEC && now > 0 && rows) {
    info.logNow = now;
    info.logRows = rows;
    info.logBytes = bytes;
  }
  return true;
}

}  // namespace

Cell cellFromRgb565(uint16_t v) {
  if (v == 0 || v == 0xFFFF) return 0;
  const int r = ((v >> 11) & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = (v & 31) * 255 / 31;
  int best = 0x7fffffff, bi = 0;
  for (int i = 0; i < CELL_PALETTE_COUNT; i++) {
    const int dr = r - PALETTE_RGB[i][0], dg = g - PALETTE_RGB[i][1], db = b - PALETTE_RGB[i][2];
    const int d = dr * dr + dg * dg + db * db;
    if (d < best) {
      best = d;
      bi = i;
    }
  }
  return (Cell)(bi + 1);
}

ApiStatus decodeApiResponse(const uint8_t *buf, size_t len, EventRecord &rec, ApiInfo &info) {
  info = ApiInfo();
  EventData &out = rec.data;
  // 応答に fit がなければ「推定できなかった」(表示しない)
  rec.fit = FitResult();
  rec.fit.done = true;
  rec.fit.hideReason = HIDE_NO_RESULT;
  Reader r{buf, buf + len};
  size_t n;
  if (!r.readMapHead(n)) return info.status = ApiStatus::MALFORMED;
  PlaneRef planes[3];
  OdRef od[3];
  int64_t statusCode = -1, timestamp = 0, timestampMs = 0, crc = 0;
  bool havePlanes = false;
  for (size_t i = 0; i < n && r.ok; i++) {
    const uint8_t *k;
    size_t kl;
    if (!r.readBytes(3, k, kl)) break;
    if (r.keyIs(k, kl, "statusCode")) r.readInt(statusCode);
    else if (r.keyIs(k, kl, "timestamp")) r.readInt(timestamp);
    else if (r.keyIs(k, kl, "timestampMs")) r.readInt(timestampMs);
    else if (r.keyIs(k, kl, "serverTimeMs")) r.readInt(info.serverTimeMs);
    else if (r.keyIs(k, kl, "clients")) r.readInt(info.clients);
    else if (r.keyIs(k, kl, "pixelsCrc32")) r.readInt(crc);
    else if (r.keyIs(k, kl, "errorMsg")) {
      const uint8_t *s;
      size_t sl;
      if (r.readBytes(3, s, sl)) {
        const size_t c = sl < sizeof(info.errorMsg) - 1 ? sl : sizeof(info.errorMsg) - 1;
        memcpy(info.errorMsg, s, c);
        info.errorMsg[c] = 0;
      }
    } else if (r.keyIs(k, kl, "planes")) {
      havePlanes = readFaces(r, planes, [](Reader &rr, PlaneRef &p) { readPlane(rr, p); });
    } else if (r.keyIs(k, kl, "od")) {
      readFaces(r, od, [](Reader &rr, OdRef &o) { readOd(rr, o); });
    } else if (r.keyIs(k, kl, "fit")) {
      readFit(r, rec.fit);
    } else if (r.keyIs(k, kl, "log")) {
      readLog(r, info);
    } else {
      r.skip();
    }
  }
  if (!r.ok) return info.status = ApiStatus::MALFORMED;
  info.statusCode = (int)statusCode;
  if (statusCode != 0) return info.status = ApiStatus::SERVER_ERROR;
  if (!havePlanes || planes[1].w != SIDE_COLS || planes[1].h != SIDE_ROWS || planes[0].w != CAP_COLS ||
      planes[0].h != CAP_COLS || planes[2].w != CAP_COLS || planes[2].h != CAP_COLS) {
    return info.status = ApiStatus::MALFORMED;
  }

  out.timestamp = (uint32_t)timestamp;
  out.timestampMs = timestampMs > 0 ? timestampMs : timestamp * 1000;
  out.crc32 = (uint32_t)crc;
  int lit = 0;
  const int side = convertPlane(planes[1].pixels, planes[1].bytes, SIDE_COLS, SIDE_ROWS, out.cells);
  const int top = convertPlane(planes[0].pixels, planes[0].bytes, CAP_COLS, CAP_COLS, out.cells + N_SIDE);
  const int bottom =
      convertPlane(planes[2].pixels, planes[2].bytes, CAP_COLS, CAP_COLS, out.cells + N_SIDE + N_CAP);
  if (side < 0 || top < 0 || bottom < 0) return info.status = ApiStatus::MALFORMED;
  lit = side + top + bottom;
  out.litCount = (uint16_t)lit;

  // OD は数だけ数える (1 ヒット 6 バイト)
  int count = 0;
  for (int f = 0; f < 3; f++) count += (int)(od[f].bytes / 6);
  out.odCount = (uint16_t)count;
  info.status = lit < MIN_LIT_PMTS ? ApiStatus::FEW_LIT : ApiStatus::OK;
  return info.status;
}

}  // namespace tkapp
