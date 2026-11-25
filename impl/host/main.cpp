// ホスト版 (確認用)。api.php?fmt=bin の応答ファイル (CBOR、推定結果 fit 入り) を順に流し込み、
// 指定した時刻のフレームを PNG に書く。描画は実機と同じく帯ごとに行う。
//
//   tkapp_host [--size WxH] [--fps N] [--strip ROWS] [--interval SEC] [--out DIR]
//              [--snap t1,t2,...] [--seconds T] [--drag X0,Y0,X1,Y1,T0,T1] [--key NAME,T]
//              [--tap X,Y,T] [--settings] [--bench] [--viewers N] file.cbor...
//
//   --interval  ファイルを流し込む間隔 (既定 15 秒。0 なら全部を t=0 で)
//   --snap      PNG を書く時刻。--out のディレクトリに frame_<秒>.png
//   --drag      時刻 T0 から T1 まで (X0,Y0) から (X1,Y1) へドラッグする
//   --tap       時刻 T に (X,Y) をタップ
//   --key       時刻 T にキー (left/right/up/down/a/b) を押して 0.3 秒後に離す
//   --settings  起動時に WiFi 設定画面を開く
//   --bench     1 フレームの描画時間を測って表示する
//   --viewers   左下の接続数をこの値で表示する (既定は最後に読んだ応答の clients。無ければ出さない)
//
// 応答にイベントの記録 (api.php?fmt=bin&log=-480 の log) があれば、取り込んで最下部に描く

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "tkapp/app.hpp"
#include "tkapp/cbor.hpp"

namespace g2 = shapoco::gfx2d;
using namespace tkapp;

namespace {

constexpr int MAX_W = 720, MAX_H = 1280;
constexpr size_t ARENA_BYTES = 96 * 1024;

alignas(8) uint8_t g_arena[ARENA_BYTES];
uint16_t g_fb[MAX_W * MAX_H];  // RGB565_SWAPPED
EventRecord g_history[HISTORY_MAX];
uint16_t g_texPixels[TEXTURE_PIXELS];
EventRecord g_incoming;
EventLog g_log;
g2::Graphics2D g_gfx;
alignas(8) uint8_t g_gfxArena[16384];
App g_app;

class HostPlatform : public Platform {
 public:
  void applyWifiConfig(const WifiConfig &cfg) override {
    std::printf("wifi config: ssid '%s' pass %u chars ip '%s' mask '%s' gw '%s' dns '%s'\n", cfg.ssid,
                (unsigned)strlen(cfg.pass), cfg.ip, cfg.mask, cfg.gateway, cfg.dns);
    state_ = WifiState::CONNECTING;
  }
  WifiState wifiState() override { return state_; }
  bool fetching() override { return fetching_; }
  int viewerCount() override { return viewers_; }
  bool fetching_ = false;
  int viewers_ = -1;
  void log(const char *msg) override { std::printf("app: %s\n", msg); }
  WifiState state_ = WifiState::CONNECTED;
};

// --- PNG (無圧縮の deflate) ---------------------------------------------------

uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return crc;
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24));
  v.push_back((uint8_t)(x >> 16));
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
  put32(out, (uint32_t)data.size());
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  out.insert(out.end(), td.begin(), td.end());
  put32(out, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

bool writePng(const char *path, const uint16_t *fb, int w, int h) {
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 3 + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      const uint16_t s = fb[y * w + x];
      const uint16_t v = (uint16_t)((s >> 8) | (s << 8));  // SWAPPED → ネイティブ
      raw.push_back((uint8_t)(((v >> 11) & 31) * 255 / 31));
      raw.push_back((uint8_t)(((v >> 5) & 63) * 255 / 63));
      raw.push_back((uint8_t)((v & 31) * 255 / 31));
    }
  }
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
    z.push_back(pos + n == raw.size() ? 1 : 0);
    z.push_back((uint8_t)n);
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)~n);
    z.push_back((uint8_t)(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w);
  put32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
  return true;
}

bool readFile(const char *path, std::vector<uint8_t> &out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return true;
}

struct Drag {
  int x0, y0, x1, y1;
  float t0, t1;
  bool started = false, finished = false;
};
struct Tap {
  int x, y;
  float t;
  bool done = false;
};
struct KeyEvent {
  Key key;
  float t;
  int state = 0;  // 0: まだ、1: 押した、2: 離した
};

Key parseKey(const char *s) {
  if (!strcmp(s, "left")) return Key::LEFT;
  if (!strcmp(s, "right")) return Key::RIGHT;
  if (!strcmp(s, "up")) return Key::UP;
  if (!strcmp(s, "down")) return Key::DOWN;
  if (!strcmp(s, "b")) return Key::B;
  return Key::A;
}

double nowUs() {
  using namespace std::chrono;
  return (double)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char **argv) {
  int w = 240, h = 320, fps = 30, stripH = 64;
  float interval = 15.0f, seconds = -1;
  std::string outDir = ".";
  std::vector<float> snaps;
  std::vector<Drag> drags;
  std::vector<Tap> taps;
  std::vector<KeyEvent> keys;
  std::vector<const char *> files;
  bool bench = false, settings = false;
  int viewersOpt = -1, lastClients = -1;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
    if (!strcmp(a, "--size")) {
      if (std::sscanf(next(), "%dx%d", &w, &h) != 2 || w < 64 || h < 64 || w > MAX_W || h > MAX_H) {
        std::fprintf(stderr, "bad size\n");
        return 1;
      }
    } else if (!strcmp(a, "--fps")) fps = std::atoi(next());
    else if (!strcmp(a, "--strip")) stripH = std::atoi(next());
    else if (!strcmp(a, "--interval")) interval = (float)std::atof(next());
    else if (!strcmp(a, "--seconds")) seconds = (float)std::atof(next());
    else if (!strcmp(a, "--out")) outDir = next();
    else if (!strcmp(a, "--bench")) bench = true;
    else if (!strcmp(a, "--viewers")) viewersOpt = atoi(next());
    else if (!strcmp(a, "--settings")) settings = true;
    else if (!strcmp(a, "--snap")) {
      const char *s = next();
      while (*s) {
        snaps.push_back((float)std::atof(s));
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
      }
    } else if (!strcmp(a, "--drag")) {
      Drag d;
      if (std::sscanf(next(), "%d,%d,%d,%d,%f,%f", &d.x0, &d.y0, &d.x1, &d.y1, &d.t0, &d.t1) != 6) {
        std::fprintf(stderr, "bad --drag\n");
        return 1;
      }
      drags.push_back(d);
    } else if (!strcmp(a, "--tap")) {
      Tap t;
      if (std::sscanf(next(), "%d,%d,%f", &t.x, &t.y, &t.t) != 3) {
        std::fprintf(stderr, "bad --tap\n");
        return 1;
      }
      taps.push_back(t);
    } else if (!strcmp(a, "--key")) {
      char name[16] = {};
      float t = 0;
      if (std::sscanf(next(), "%15[a-z],%f", name, &t) != 2) {
        std::fprintf(stderr, "bad --key\n");
        return 1;
      }
      keys.push_back({parseKey(name), t});
    } else {
      files.push_back(a);
    }
  }
  if (seconds < 0) {
    seconds = 1;
    for (float s : snaps) if (s + 0.01f > seconds) seconds = s + 0.01f;
  }

  HostPlatform platform;
  Buffers buffers;
  buffers.history = g_history;
  buffers.historyCapacity = HISTORY_MAX;
  buffers.texturePixels = g_texPixels;
  if (!g_app.init(w, h, buffers, &platform, g_arena, sizeof g_arena)) {
    std::fprintf(stderr, "app init failed\n");
    return 1;
  }
  g_gfx.init(g_gfxArena, sizeof g_gfxArena);
  WifiConfig wc;
  strcpy(wc.ssid, "example");
  g_app.setWifiConfig(wc);
  if (settings) g_app.keyDown(Key::B);

  // 推定結果は応答の fit (サーバが入れる)
  std::vector<EventRecord *> records;
  for (const char *path : files) {
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) {
      std::fprintf(stderr, "cannot read %s\n", path);
      return 1;
    }
    EventRecord *rec = new EventRecord();
    ApiInfo info;
    const ApiStatus st = decodeApiResponse(buf.data(), buf.size(), *rec, info);
    if (info.clients >= 0) lastClients = (int)info.clients;
    if (info.logNow > 0) g_log.merge(info.logNow, info.logRows, info.logBytes);
    if (st == ApiStatus::MALFORMED || st == ApiStatus::SERVER_ERROR) {
      std::fprintf(stderr, "%s: bad response (status %d, code %d, '%s')\n", path, (int)st, info.statusCode, info.errorMsg);
      return 1;
    }
    static const char *kinds[] = {"through", "stopping", "inside", "contained"};
    std::printf("%s: lit %u od %u -> %s%s corr %.3f hide %d (%u ms)%s\n", path, rec->data.litCount, rec->data.odCount,
                kinds[(int)rec->fit.kind], rec->fit.show ? "" : " (hidden)", (double)rec->fit.corr, rec->fit.hideReason,
                rec->fit.fitMs, st == ApiStatus::FEW_LIT ? " [few lit]" : "");
    records.push_back(rec);
  }
  platform.viewers_ = viewersOpt >= 0 ? viewersOpt : lastClients;
  if (g_log.nowSlot() > 0) g_app.updateLog(g_log);


  const float dt = 1.0f / fps;
  const int frames = (int)(seconds * fps + 0.5f);
  size_t nextRecord = 0;
  size_t nextSnap = 0;
  double drawUs = 0, advanceUs = 0;
  int drawn = 0;
  for (int frame = 0; frame <= frames; frame++) {
    const float t = frame * dt;
    // データの投入
    while (nextRecord < records.size() && t + 1e-4f >= interval * nextRecord) {
      g_app.pushEvent(*records[nextRecord]);
      nextRecord++;
    }
    // 取得中のまね: 次の投入の前 1.5 秒
    platform.fetching_ = interval > 0 && nextRecord < records.size() && interval * nextRecord - t < 1.5f;
    // 操作
    for (Drag &d : drags) {
      if (!d.started && t >= d.t0) {
        d.started = true;
        g_app.pointerDown(0, d.x0, d.y0);
      } else if (d.started && !d.finished) {
        const float k = d.t1 > d.t0 ? (t - d.t0) / (d.t1 - d.t0) : 1;
        if (k >= 1) {
          g_app.pointerMove(0, d.x1, d.y1);
          g_app.pointerUp(0);
          d.finished = true;
        } else {
          g_app.pointerMove(0, (int)(d.x0 + (d.x1 - d.x0) * k), (int)(d.y0 + (d.y1 - d.y0) * k));
        }
      }
    }
    for (Tap &tp : taps) {
      if (!tp.done && t >= tp.t) {
        g_app.pointerDown(0, tp.x, tp.y);
        g_app.pointerUp(0);
        tp.done = true;
      }
    }
    for (KeyEvent &k : keys) {
      if (k.state == 0 && t >= k.t) { g_app.keyDown(k.key); k.state = 1; }
      else if (k.state == 1 && t >= k.t + 0.3f) { g_app.keyUp(k.key); k.state = 2; }
    }

    const double a0 = nowUs();
    g_app.advance(dt);
    g_app.buildScene();
    const double a1 = nowUs();
    for (int y = 0; y < h; y += stripH) {
      const int rows = stripH < h - y ? stripH : h - y;
      const g2::Surface strip = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, w, rows, g_fb + (size_t)y * w);
      g_app.renderStrip(0, g_gfx, strip, y);
    }
    g_app.endFrame();
    const double a2 = nowUs();
    advanceUs += a1 - a0;
    drawUs += a2 - a1;
    drawn++;

    while (nextSnap < snaps.size() && t + 1e-4f >= snaps[nextSnap]) {
      char path[512];
      std::snprintf(path, sizeof path, "%s/frame_%06.2f.png", outDir.c_str(), (double)snaps[nextSnap]);
      if (!writePng(path, g_fb, w, h)) std::fprintf(stderr, "cannot write %s\n", path);
      else std::printf("wrote %s\n", path);
      nextSnap++;
    }
  }
  const Stats &s = g_app.stats();
  std::printf("%d frames: advance %.2f ms, draw %.2f ms per frame; tris %u, span peak %u, tri bytes %u\n", drawn,
              advanceUs / drawn / 1000, drawUs / drawn / 1000, s.triCount, s.spanPeak, s.triBytes);
  (void)bench;
  return 0;
}
