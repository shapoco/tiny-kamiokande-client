#ifndef TKAPP_CONFIG_HPP
#define TKAPP_CONFIG_HPP

// アプリ全体の定数。Web 版 (index.html の FIT, COMPASS など) と同じ値を使う。
// 単位は PMT の配置ピッチ、時間は秒、色は 0..1 の RGB。

#include <cstddef>
#include <cstdint>

namespace tkapp {

#define TKAPP_DISP_NAME "Tiny Kamiokande"
#define TKAPP_API_URL "https://www.shapoco.net/tiny-kamiokande/api.php?fmt=bin"

// 検出器
constexpr int SIDE_COLS = 150, SIDE_ROWS = 51, CAP_COLS = 48;
constexpr float RADIUS = CAP_COLS / 2.0f;
constexpr float HEIGHT = (float)SIDE_ROWS;
constexpr int N_SIDE = SIDE_COLS * SIDE_ROWS, N_CAP = CAP_COLS * CAP_COLS;
constexpr int N_PMTS = N_SIDE + 2 * N_CAP;

// 応答の fit.hide (サーバが判定した、軌跡を表示しない理由。SPEC.md 2.3)。値はサーバと共通なので変えないこと
enum HideReason : uint8_t {
  HIDE_NONE = 0,       // 表示する
  HIDE_NO_RESULT = 1,  // 推定できなかった (応答に fit が無い・null も含む)
  HIDE_LIT = 2,        // 点灯が少ない
  HIDE_CORR = 3,       // 当てはまりが弱い
  HIDE_AMBIGUOUS = 4,  // 別解と区別できない
  HIDE_OD = 5,         // 外水槽のヒットと不一致
};

// テクスチャ (2 のべき乗。1 PMT = 1 テクセル)。3 面を 1 枚に入れる: 側面 150×51 を左上に、
// 上面と底面 (48×48) をその右の空き (106 列) に並べる。間は黒の隙間 (テクスチャの読み出しの誤差でにじまないよう)
constexpr int FACE_TEX_W = 256, FACE_TEX_H = 64;
constexpr int CAP_TOP_TEX_X = 152, CAP_BOTTOM_TEX_X = 204;  // 上面・底面の左端の列 (上端は 0 行)
static_assert(SIDE_COLS + 2 <= CAP_TOP_TEX_X && CAP_TOP_TEX_X + CAP_COLS + 4 <= CAP_BOTTOM_TEX_X &&
                  CAP_BOTTOM_TEX_X + CAP_COLS <= FACE_TEX_W && CAP_COLS <= FACE_TEX_H && SIDE_ROWS <= FACE_TEX_H,
              "the faces do not fit in the texture");

// 円筒の分割数。描画の時間はスキャンラインごとのスパン数 (≒ 見えている三角形の数) に比例するので少なめにする。
// 上下面は分割せず正方形 2 枚で描く (円の外はテクスチャが黒で、加算合成なので何も足されない)
constexpr int SIDE_SEGMENTS = 48;
constexpr int SIDE_DIVS = 1;  // 高さ方向の分割
constexpr int RIM_SEGMENTS = 64;

// 取得と表示の間隔
constexpr float FETCH_INTERVAL_SEC = 9.0f;  // 取得の開始から次の開始まで。サーバのキャッシュ (10 秒) より短くして取りこぼさない
// 1 件を最低この時間表示してから次へ切り替える。表示の時刻は表示待ちのキュー (QUEUE_DELAY_SEC) が決めるので 0 (端末間の同期のため)
constexpr float SHOW_MIN_SEC = 0.0f;
// 表示待ちのキュー (端末間の同期)。イベントは timestampMs (サーバ時刻) から QUEUE_DELAY_SEC 経ったら表示する。
// 最初のイベントが届いたときは遅れ 0 で、QUEUE_RAMP_SEC かけて QUEUE_DELAY_SEC まで伸ばす (Web 版と同じ。SPEC.md 3.3)
constexpr float QUEUE_DELAY_SEC = 15.0f;
constexpr float QUEUE_RAMP_SEC = 60.0f;
constexpr int MIN_LIT_PMTS = 200;         // 色の付いた PMT がこれ未満のデータは空の画像とみなして捨てる
// 履歴機能 (▶ LIVE と履歴のボタン、過去のイベントの再生)。ビルド時に TKAPP_HISTORY で選ぶ。
// 無効のときも表示中の 1 件と次に届く 1 件を置く領域は要る
#ifndef TKAPP_HISTORY
#define TKAPP_HISTORY 1
#endif
constexpr bool HISTORY_ENABLED = TKAPP_HISTORY != 0;
constexpr int HISTORY_MAX = HISTORY_ENABLED ? 20 : 2;

// カメラ
constexpr float CAMERA_FOV_DEG = 60.0f;
constexpr float CAMERA_NEAR = 0.5f, CAMERA_FAR = 2000.0f;
constexpr float CAMERA_MIN_DIST = 0.6f, CAMERA_MAX_DIST = 400.0f;
// Graphics3D の 1 走査線あたりのスパンの上限 (描画コンテキストごと)。実測のピークは 90〜100。
// 既定 (512) より絞って、2 コア描画で増える 2 つ目のコンテキストの分を作業領域から捻出する
constexpr int SPAN_CAPACITY = 256;
constexpr float FIT_MARGIN = 1.15f;       // 外接球の周囲に残す余白の比率
constexpr float CAMERA_DAMPING = 0.08f;   // OrbitControls の dampingFactor 相当 (1 フレーム 1/60 秒基準)
constexpr float IDLE_BEFORE_AUTO_ROTATE_SEC = 5.0f;
constexpr float AUTO_ROTATE_RAMP_SEC = 3.0f;
constexpr float AUTO_ROTATE_SPEED = 0.12f;   // rad/s
constexpr float KEY_ROTATE_SPEED = 1.2f;     // rad/s
constexpr float KEY_ZOOM_RATE = 1.0f;        // 1 秒あたりの距離倍率の対数
constexpr float DRAG_ROTATE_PER_PX = 6.2832f / 400.0f;  // 画面 400 px のドラッグで 1 回転
constexpr float PITCH_LIMIT = 1.5533f;       // 89 度

// 演出 (index.html の FIT)
constexpr float FADE_OUT_SEC = 0.5f;
constexpr float TEXTURE_FADE_OUT_SEC = 0.5f;
constexpr float TEXTURE_FADE_IN_SEC = 1.0f;
constexpr float REVEAL_BLUR = 3.5f;
constexpr float METEOR_SPEED = 60.0f;
constexpr float METEOR_HEAD_SIZE = 5.0f;
constexpr float METEOR_HEAD_COLOR_OUTSIDE[3] = {0.6f, 0.8f, 1.0f};
constexpr float METEOR_HEAD_COLOR_INSIDE[3] = {1.0f, 0.75f, 0.65f};
constexpr float METEOR_TAIL_BOOST = 0.8f;
constexpr float METEOR_TAIL_LEN = 10.0f;
constexpr float FLASH_SEC = 0.4f;
constexpr float FLASH_SCALE = 3.5f;
constexpr float BURST_COLOR[3] = {1.0f, 0.9f, 0.8f};
constexpr float BURST_AURA_SCALE = 5.0f;
constexpr float BURST_AURA_SEC = 0.7f;
constexpr float BURST_CROSS_SIZE = 70.0f;
constexpr float BURST_CROSS_SEC = 0.9f;
constexpr float BURST_CROSS_TURN = 0.3f;
constexpr float BURST_RIPPLE_SIZE = 90.0f;
constexpr float BURST_RIPPLE_WIDTH = 3.0f;
constexpr float BURST_RIPPLE_SEC = 1.3f;
constexpr float STOP_BURST_STRENGTH = 0.5f;
constexpr float BAND_COLOR[3] = {1.0f, 0.6f, 0.5f};
constexpr float BAND_COLOR_OUTSIDE[3] = {0.1f, 0.2f, 0.667f};
constexpr float BAND_HALF_WIDTH = 3.6f;      // Web 版の 2 倍 (小さい画面では細いと見づらい)
constexpr float BAND_BRIGHTNESS_GAIN = 2.0f;  // 帯の明るさの倍率 (同上)
constexpr float ENTRY_BRIGHTNESS = 0.35f;
constexpr float BAND_EXTEND = 60.0f;
constexpr int BAND_SECTIONS = 48;
constexpr int BAND_ROWS = BAND_SECTIONS + 1 + 4 + 2;
constexpr float AURA_SIZE = 9.0f;
constexpr float AURA_SIZE_EVENT = 18.0f;
constexpr float AURA_FLICKER = 0.5f;
constexpr float AURA_FLICKER_SIZE = 0.12f;
constexpr float AURA_FLICKER_HZ = 2.0f;
constexpr float AURA_STAR_SIZE = 1.8f;
constexpr float AURA_STAR_COLOR[3] = {1.0f, 0.5f, 0.4f};
constexpr float AURA_COLOR_WALL[3] = {0.2f, 0.6f, 1.0f};
constexpr float AURA_COLOR_EVENT[3] = {1.0f, 0.2f, 0.15f};

// テクスチャの色
constexpr uint8_t UNLIT_RGB[3] = {22, 22, 22};
constexpr float TEXTURE_GAIN = 1.0f / 3.0f;   // 1 PMT = 1 テクセルで加算すると白飛びしやすいので暗くする
constexpr float PURPLE_DARKEN = 0.65f;

// 方位 (index.html の COMPASS)
constexpr float NORTH_PHI_SK_DEG = 215.0f;
constexpr float COMPASS_RADIUS = RADIUS * 1.12f;
constexpr float COMPASS_Y = -HEIGHT / 2 - 1.5f;
constexpr uint32_t COMPASS_RING_COLOR = 0xFF303030u;   // ARGB8888
constexpr uint32_t COMPASS_TICK_COLOR = 0xFF707070u;
constexpr uint32_t COMPASS_LABEL_COLOR = 0xFFAAAAAAu;
constexpr uint32_t COMPASS_NORTH_COLOR = 0xFFFF8080u;
constexpr uint32_t RIM_COLOR = 0xFF404040u;
constexpr float COMPASS_LABEL_OFFSET = 2.4f + 4.5f * 0.6f;  // リングから N/E/S/W の中心まで
constexpr float FACILITY_RADIUS = COMPASS_RADIUS + 7.0f;
constexpr float FACILITY_TICK = 2.0f;
struct Facility {
  const char *name;
  float azimuthDeg;  // 北から時計回り
  uint32_t color;    // ARGB8888
};
constexpr Facility FACILITIES[] = {
    {"J-PARC", 88.6f, 0xFF80C8FFu},
};
constexpr int FACILITY_COUNT = (int)(sizeof(FACILITIES) / sizeof(FACILITIES[0]));

// UI
constexpr int UI_MARGIN = 6;             // 画面の端からの余白 [px]
constexpr int UI_BUTTON_H = 22;          // LIVE / 履歴ボタンの高さ
constexpr int UI_LIVE_W = 54;
constexpr int UI_HISTORY_W = 14;         // 履歴ボタンの幅
constexpr int UI_HISTORY_GAP = 3;
constexpr int UI_TOUCH_SLOP = 6;         // 当たり判定を広げる量
constexpr int UI_ICON_MIN = 24;          // WiFi アイコンの大きさの下限 (CoreS3 の 240×320 でこの値)
constexpr int UI_ICON_DIV = 20;          // 大きさは画面の短辺 / この値 (Tab5 の 360×640 では下限と同じ 24 になる)
constexpr int TAP_MAX_MOVE_PX = 8;       // タップとみなす移動量の上限
constexpr float TAP_MAX_SEC = 0.5f;
// シングルタッチの機種 (setMultiTouch(false)) のズーム: 3D の表示をダブルタップで拡大、長押しで縮小
constexpr float DOUBLE_TAP_SEC = 0.4f;     // 1 回目のタップを離してから 2 回目を押すまで
constexpr int DOUBLE_TAP_MAX_DIST_PX = 24; // 1 回目と 2 回目の位置の差の上限
constexpr float DOUBLE_TAP_ZOOM = 0.6f;    // 1 回の拡大で距離に掛ける値
constexpr float HOLD_ZOOM_SEC = 0.5f;      // 動かさずにこれだけ押し続けたら縮小を始める
constexpr float HOLD_ZOOM_RATE = 1.0f;     // 縮小の速さ (1 秒あたりの距離倍率の対数)
static_assert(HOLD_ZOOM_SEC >= TAP_MAX_SEC, "a hold must not end as a tap");
constexpr uint32_t COLOR_KIND_THROUGH = 0xFF2070E0u;
constexpr uint32_t COLOR_KIND_STOPPING = 0xFFE0A020u;
constexpr uint32_t COLOR_KIND_INSIDE = 0xFFE03030u;
constexpr uint32_t COLOR_KIND_HIDDEN = 0xFF555555u;
constexpr uint32_t COLOR_KIND_FITTING = 0xFF202020u;
constexpr uint32_t COLOR_BUTTON_BG = 0xFF202020u;
constexpr uint32_t COLOR_BUTTON_FRAME = 0xFF606060u;
constexpr uint32_t COLOR_SELECTED_FRAME = 0xFFFFFFFFu;
constexpr uint32_t COLOR_TEXT = 0xFFE0E0E0u;
constexpr uint32_t COLOR_TEXT_DIM = 0xFF909090u;
constexpr uint32_t COLOR_PANEL_BG = 0xFF101418u;
constexpr uint32_t COLOR_KEY_BG = 0xFF303840u;
constexpr uint32_t COLOR_KEY_PRESSED = 0xFF6080A0u;
constexpr uint32_t COLOR_FIELD_BG = 0xFF181C20u;
constexpr uint32_t COLOR_FIELD_ACTIVE = 0xFF405060u;
constexpr uint32_t COLOR_WIFI_CONNECTED = 0xFF606060u;   // 接続済みは目立たない灰色
constexpr uint32_t COLOR_WIFI_FETCHING = 0xFF3C6E8Cu;    // api.php から取得中。接続済みの灰色と同じくらいの明るさの暗い空色
constexpr uint32_t COLOR_VIEWERS = COLOR_WIFI_CONNECTED;  // 左上の接続数。WiFi アイコンと同じ明るさ


// 設定の文字列の長さ (終端を含まない)
constexpr int SSID_MAX = 32;
constexpr int PASS_MAX = 63;
constexpr int IPV4_MAX = 15;

}  // namespace tkapp

#endif
