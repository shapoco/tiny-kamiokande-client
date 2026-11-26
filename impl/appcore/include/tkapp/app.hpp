#ifndef TKAPP_APP_HPP
#define TKAPP_APP_HPP

// アプリ本体 (機種非依存)。3D 表示、演出、履歴、UI、入力をまとめる。
// プラットフォームは Platform を実装し、フレームごとに advance() → buildScene() →
// 帯ごとに renderStrip() → endFrame() を呼ぶ。推定とデータ取得は別スレッド (コア) で行い、
// 終わったものを pushEvent() で渡す (描画と同じスレッドから呼ぶこと)。
// ヒープは使わない。履歴とテクスチャの置き場所は Buffers で渡す。App 自体は PSRAM に置いてよい
// (毎ピクセル読むのはテクスチャと Graphics3D の arena だけ)

#include <cstddef>
#include <cstdint>

#include "shapoco/gfx2d/graphics2d.hpp"
#include "shapoco/gfx3d/gfx3d.hpp"
#include "tkapp/config.hpp"
#include "tkapp/detector.hpp"
#include "tkapp/event.hpp"
#include "tkapp/eventlog.hpp"

namespace tkapp {

namespace g2 = shapoco::gfx2d;
namespace g3 = shapoco::gfx3d;

struct WifiConfig {
  char ssid[SSID_MAX + 1] = {};
  char pass[PASS_MAX + 1] = {};
  char ip[IPV4_MAX + 1] = {};       // 空なら DHCP
  char mask[IPV4_MAX + 1] = {};
  char gateway[IPV4_MAX + 1] = {};
  char dns[IPV4_MAX + 1] = {};
};

enum class WifiState : uint8_t { NO_CONFIG, CONNECTING, CONNECTED, FAILED, UNAVAILABLE };

// プラットフォームが実装する
class Platform {
 public:
  virtual ~Platform() = default;
  // 設定を保存して接続し直す
  virtual void applyWifiConfig(const WifiConfig &cfg) = 0;
  virtual WifiState wifiState() = 0;
  // api.php から取得している最中か (WiFi アイコンの色を変える)
  virtual bool fetching() { return false; }
  // 接続数 (api.php の clients)。負なら表示しない
  virtual int viewerCount() { return -1; }
  virtual void log(const char *msg) { (void)msg; }
};

enum class Key : uint8_t { LEFT, RIGHT, UP, DOWN, A, B, COUNT };
constexpr int MAX_POINTERS = 2;

// 履歴の実体。HISTORY_MAX 個の EventRecord をプラットフォームが確保する (PSRAM 可)
struct Buffers {
  EventRecord *history = nullptr;      // PSRAM 可
  int historyCapacity = 0;
  uint16_t *texturePixels = nullptr;   // TEXTURE_PIXELS 個。内蔵 RAM に置く
};

struct HistoryEntry {
  uint32_t id = 0;  // 0 は空き
  EventRecord *rec = nullptr;
};

enum class TexPhase : uint8_t { FADE_OUT, BLACK, INTRO, FADE_IN, SHOW };
enum class Screen : uint8_t { MAIN, WIFI_MENU, KEYBOARD };

// 流星または閃光の導入アニメーション
struct Intro {
  bool burst = false;  // true: 閃光 (検出器内で発生)、false: 流星
  float t = 0;
  float sMin, sMax, sIn, sOut, v, revealEnd, tIn, tEvent;
};

struct BurstFx {
  bool active = false, done = false;
  float t = 0, strength = 1;
  float pos[3];
};

struct Stats {
  float fps = 0;
  int frames = 0;
  uint32_t triCount = 0;
  uint32_t spanPeak = 0;
  uint32_t triBytes = 0;
};

class App {
 public:
  // w, h: フレームの大きさ。arena: Graphics3D の作業領域 (内蔵 RAM)。renderContexts: 同時に render() するコア数
  bool init(int w, int h, const Buffers &buffers, Platform *platform, void *arena, size_t arenaBytes,
            int renderContexts = 1);

  // false: シングルタッチの機種。ピンチの代わりに 3D の表示のダブルタップで拡大、長押しで縮小する
  void setMultiTouch(bool on) { multiTouch_ = on; }
  void setWifiConfig(const WifiConfig &cfg) { wifi_ = cfg; }
  const WifiConfig &wifiConfig() const { return wifi_; }

  // --- 入力 (描画スレッド) ---
  void pointerDown(int id, int x, int y);
  void pointerMove(int id, int x, int y);
  void pointerUp(int id);
  void keyDown(Key k);
  void keyUp(Key k);

  // --- データ (描画スレッド) ---
  // 推定の終わったイベントを履歴に積む。LIVE なら表示の切り替えを予定する
  void pushEvent(const EventRecord &rec);
  int historyCount() const { return historyCount_; }
  // サーバのイベントの記録が更新されたら呼ぶ。画面最下部の画像を作り直す
  void updateLog(const EventLog &log);
  int logCols() const { return logCols_; }

  // --- フレーム ---
  void advance(float dt);
  // 3D シーンを組み立てて beginRender() まで行う。フレームごとに 1 回
  void buildScene();
  // フレームの [y, y + target.height) 行を描く。ctx は Graphics3D のレンダーコンテキスト。
  // 複数コアから異なる ctx で同時に呼べる
  void renderStrip(int ctx, g2::Graphics2D &g, const g2::Surface &target, int y);
  void endFrame();

  const Stats &stats() const { return stats_; }
  Screen screen() const { return screen_; }

 private:
  // --- カメラ ---
  struct Camera {
    float yaw, pitch, dist;           // 現在値
    float yawT, pitchT, distT;        // 目標値 (ダンピング)
    float eye[3], right[3], up[3], fwd[3];
    float f, aspect;                  // 投影: f = 1/tan(fov/2)
    float fittedDist;
    float idleSec;
    float autoRotateSpeed;
  };
  void initCamera();
  float fitCameraDistance() const;
  void updateCamera(float dt, bool interacting);
  // モデル座標 → 画面座標。カメラの後ろなら false
  bool project(const float p[3], float &sx, float &sy, float &depth) const;

  // --- 表示の切り替え (anim.cpp) ---
  void beginTransition(HistoryEntry *entry, bool instant);
  void maybeStartIntro();
  void startIntro();
  void setTrack(const FitResult &fit);
  void updateIntro(float dt);
  void updateMeteor(float dt);
  void updateBurstIntro(float dt);
  bool updateBurstFx(float dt);
  void startBurst(const float *p, float strength);
  void updateTransition(float dt);
  void trackInterval(float &L, float &sMin, float &sMax, float &sIn, float &sOut) const;
  float bandBrightness(float sPos, float L, float sMax) const;
  void setEventAura(float opacity, float sizeFactor);
  float pinkNoise(float t) const;

  // --- シーン (scene.cpp) ---
  void initScene();
  void buildDetectorGeometry();
  void makeSpriteTextures();
  void putDetector();
  void putCompass();
  void putBand(float sHead);
  void putSprite(const float *pos, float size, const float *rgb, float opacity, const g2::Texture *tex,
                 float angle = 0);
  void putRipple(const float *pos, float radius, float width, const float *rgb, float opacity);
  void drawLabels(g2::Graphics2D &g, int y);

  // --- イベントの記録 (ui.cpp) ---
  void layoutLog();
  void drawLog(g2::Graphics2D &g);

  // --- UI (ui.cpp) ---
  struct Button {
    int x, y, w, h;
    int kind;  // 0: LIVE、1..: 履歴 (index + 1)、-1: WiFi アイコン
    int history;
  };
  void layoutButtons();
  int hitButton(int x, int y) const;  // ボタンの添字、無ければ -1
  void buttonAction(int index);
  void drawOverlay(g2::Graphics2D &g, int y);
  void drawMainUi(g2::Graphics2D &g);
  void drawWifiIcon(g2::Graphics2D &g, int x, int y, int size, WifiState st);
  void drawViewerCount(g2::Graphics2D &g, int count);
  void drawWifiMenu(g2::Graphics2D &g);
  void drawKeyboard(g2::Graphics2D &g);
  void uiPointerDown(int x, int y);
  void uiPointerUp(int x, int y);
  void uiKey(Key k);
  void openKeyboard(int field);
  void keyboardInput(int keyIndex);
  void menuSelect(int item);
  g2::Font uiFont() const;
  int iconSize() const;  // WiFi アイコンと進捗のドーナツの大きさ
  int uiFontH() const;

  // --- 履歴 ---
  HistoryEntry *findEntry(uint32_t id);
  HistoryEntry *newestEntry();
  void trimHistory();
  void selectEntry(HistoryEntry *entry, bool live);
  uint32_t kindColor(const HistoryEntry &e) const;

  // --- 状態 ---
  int w_ = 0, h_ = 0;
  int viewH_ = 0;  // 3D 表示の高さ (最下部の記録の分を除く)

  // イベントの記録の画像 (logCols_ × LOG_SLOTS_PER_HOUR。logScale_ 倍に拡大して (logX_, viewH_) に描く)
  int logCols_ = 0, logScale_ = 1, logX_ = 0, logH_ = 0;
  bool hasLog_ = false;
  int64_t logNowSlot_ = -1;  // 画像を作ったときの現在の枠 (区切り線の位置を決める)

  uint16_t logPixels_[LOG_MAX_COLS * LOG_SLOTS_PER_HOUR];
  g2::Texture logTex_;

  Platform *platform_ = nullptr;
  bool multiTouch_ = true;
  WifiConfig wifi_;
  Detector detector_;
  g3::Graphics3D g3d_;
  Stats stats_;
  float timeSec_ = 0;
  float fpsAccum_ = 0;
  int fpsFrames_ = 0;

  // 履歴
  Buffers buffers_;
  HistoryEntry history_[HISTORY_MAX];  // 新しい順
  int historyCount_ = 0;
  uint32_t nextId_ = 1;
  bool live_ = true;
  uint32_t displayedId_ = 0;   // 表示中 (切り替え中を含む)
  uint32_t pendingLiveId_ = 0; // LIVE で次に表示する予定
  float shownSec_ = 0;         // 表示を始めてからの時間

  // カメラ
  Camera cam_;

  // 表示の切り替え
  TexPhase texPhase_ = TexPhase::BLACK;
  float texBrightness_ = 0;
  const PmtCells *pendingCells_ = nullptr;
  bool hasPending_ = false;
  Intro intro_;
  bool introActive_ = false;
  BurstFx burst_;
  float fitFade_ = 0, fitFadeTarget_ = 0, fitFadeEased_ = 0;
  bool fitVisible_ = false;
  float auraShown_[2] = {1, 1};
  bool auraIsEvent_[2] = {false, false};
  float auraPos_[2][3];
  float auraColor_[2][3];
  float auraOpacity_[2] = {0, 0};
  float auraScale_[2] = {AURA_SIZE, AURA_SIZE};
  float starOpacity_ = 0, starScale_ = 0;
  float bandShown_ = 1, bandOpacity_ = 0;
  float headPos_[3], headColor_[3], headOpacity_ = 0;
  float revealCenter_[3];
  float revealRadius_ = -1;  // < 0: 全部点いている
  Kind trackKind_ = Kind::THROUGH;
  float trackA_[3], trackB_[3], trackU_[3];
  float trackStopS_ = 0;
  // 閃光の 3 要素
  float burstAuraScale_ = 0, burstAuraColor_[3], burstAuraOpacity_ = 0;
  float burstCrossScale_ = 0, burstCrossAngle_ = 0, burstCrossOpacity_ = 0;
  float burstRippleRadius_ = 0, burstRippleWidth_ = 0, burstRippleOpacity_ = 0;
  bool texturesDirty_ = true;
  bool sceneBuilt_ = false;

  // シーン
  g3::Vertex sideVerts_[(SIDE_DIVS + 1) * (SIDE_SEGMENTS + 1)];
  uint16_t sideIdx_[SIDE_DIVS][2 * (SIDE_SEGMENTS + 1)];
  g3::Vertex capVerts_[2][4];
  g3::Vertex rimVerts_[2][RIM_SEGMENTS];
  g3::Vertex compassRing_[90];
  g3::Vertex compassTicks_[72 + 2 * FACILITY_COUNT];
  uint16_t seqIdx_[128];
  g3::Vertex bandVerts_[2 * BAND_ROWS];
  uint16_t bandIdx_[2 * BAND_ROWS];
  g3::Vertex quadVerts_[4];
  g3::Vertex rippleVerts_[3 * 33];
  uint16_t rippleIdx_[2 * 3 * 33];
  uint16_t auraPixels_[32 * 32];
  uint16_t burstAuraPixels_[64 * 64];
  uint16_t crossPixels_[64 * 64];
  uint16_t starPixels_[64 * 64];
  uint16_t bandPixels_[2 * 128];
  g2::Texture auraTex_, burstAuraTex_, crossTex_, starTex_, bandTex_;
  g3::Material matFace_[3], matLine_, matBand_, matSprite_[8], matRipple_;
  int spriteMatUsed_ = 0;
  // 画面に出す文字 (方位など)。buildScene() で位置を決め、帯の描画で描く
  struct Label {
    const char *text;
    float sx, sy;
    uint32_t color;
    bool visible;
  };
  Label labels_[4 + FACILITY_COUNT];
  int labelCount_ = 0;

  // 入力
  struct Pointer {
    bool down = false;
    int x = 0, y = 0, x0 = 0, y0 = 0;
    float sec = 0;
    bool moved = false;
    int button = -1;  // 押したボタン
    bool secondTap = false;  // ダブルタップの 2 回目として押した (シングルタッチのズーム)
    bool holding = false;    // 長押しで縮小している
  };
  Pointer ptr_[MAX_POINTERS];
  // 直前のタップ (3D の表示の上。ダブルタップの判定用)
  float lastTapSec_ = -1e9f;
  int lastTapX_ = 0, lastTapY_ = 0;
  int pinchDist0_ = 0;
  float pinchCamDist0_ = 0;
  bool keyHeld_[(int)Key::COUNT] = {};
  bool interacted_ = false;

  // UI
  Screen screen_ = Screen::MAIN;
  Button buttons_[HISTORY_MAX + 2];
  int buttonCount_ = 0;
  int pressedButton_ = -1;
  WifiConfig draft_;
  int menuFocus_ = 0;       // WiFi メニューの項目 (0..5: 項目、6: 接続、7: 戻る)
  int editField_ = -1;      // キーボードで編集中の項目
  int kbdPage_ = 0;         // 0: 小文字、1: 大文字、2: 記号
  int kbdFocus_ = 0;
  int kbdPressed_ = -1;
  char editBuf_[PASS_MAX + 1];
  int editLen_ = 0, editMax_ = 0;
};

}  // namespace tkapp

#endif
