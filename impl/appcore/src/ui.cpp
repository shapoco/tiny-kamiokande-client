// 2D の UI: 履歴の帯 (時間軸と LIVE ボタン)、WiFi アイコン、WiFi 設定メニューとキーボード

#include <cmath>
#include <cstdio>
#include <cstring>

#include "shapoco/gfx2d/fonts.hpp"
#include "tkapp/app.hpp"

namespace tkapp {

namespace {

constexpr float PI_F = 3.14159265358979f;

// メニューの項目
constexpr int FIELD_COUNT = 6;
constexpr int MENU_CONNECT = FIELD_COUNT, MENU_CANCEL = FIELD_COUNT + 1, MENU_ITEMS = FIELD_COUNT + 2;
const char *const FIELD_LABELS[FIELD_COUNT] = {"SSID", "Password", "IP (blank: DHCP)", "Netmask", "Gateway", "DNS"};

// キーボード: 4 行 × 10 キー + 下段の特殊キー
constexpr int KBD_COLS = 10, KBD_ROWS = 4;
constexpr int KEY_SHIFT = 40, KEY_SYM = 41, KEY_SPACE = 42, KEY_BKSP = 43, KEY_OK = 44, KEY_CANCEL = 45, KEY_COUNT = 46;
const char *const KBD_PAGES[3][KBD_ROWS] = {
    {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM;:_"},
    {"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~        "},
};
// 下段のキーの幅 (キー幅に対する倍率) と並び
const float KBD_BOTTOM_W[6] = {1.5f, 1.5f, 3.0f, 1.5f, 1.25f, 1.25f};
const int KBD_BOTTOM_KEYS[6] = {KEY_SHIFT, KEY_SYM, KEY_SPACE, KEY_BKSP, KEY_OK, KEY_CANCEL};

char *fieldOf(WifiConfig &c, int field, int &maxLen) {
  switch (field) {
    case 0: maxLen = SSID_MAX; return c.ssid;
    case 1: maxLen = PASS_MAX; return c.pass;
    case 2: maxLen = IPV4_MAX; return c.ip;
    case 3: maxLen = IPV4_MAX; return c.mask;
    case 4: maxLen = IPV4_MAX; return c.gateway;
    default: maxLen = IPV4_MAX; return c.dns;
  }
}

// 大文字の高さ (ベースラインから 'L' の上端まで)。GFX フォントでなければ fallback
int capHeight(const g2::Font &f, int fallback) {
  const g2::GFXfont *gf = f.gfx();
  if (!gf || 'L' < gf->first || 'L' > gf->last) return fallback;
  return -gf->glyph['L' - gf->first].yOffset;
}

}  // namespace

g2::Font App::uiFont() const {
  return h_ >= 480 ? g2::Font(&g2::ShapoSansP_s21c16a01w03) : g2::Font(&g2::ShapoSansP_s12c09a01w02);
}

int App::uiFontH() const { return h_ >= 480 ? 21 : 12; }

int App::iconSize() const {
  const int s = (w_ < h_ ? w_ : h_) / UI_ICON_DIV;
  return s > UI_ICON_MIN ? s : UI_ICON_MIN;
}

// ---------------------------------------------------------------- メイン画面のボタン

// 履歴の帯 (記録の画像のすぐ上) の右端に LIVE ボタン、その左に時間軸。右上に WiFi アイコン
void App::layoutButtons() {
  const int scale = h_ >= 480 ? 2 : 1;
  const int bh = UI_BUTTON_H * scale, liveW = UI_LIVE_W * scale, margin = UI_MARGIN * scale;
  const int dotR = UI_HIST_DOT_R * scale;
  buttonCount_ = 0;
  // LIVE ボタンは画面の右端のキワまで寄せる (輪郭線の右端が画面の最後の列に乗る)
  const int liveX = w_ - liveW;
  buttons_[buttonCount_++] = {liveX, histY_ + (histH_ - bh) / 2, liveW, bh, BUTTON_LIVE};
  axisX0_ = margin + dotR;
  axisX1_ = liveX - margin - dotR;
  // 時間軸は帯の高さ全体で受ける (LIVE ボタンが先に当たる)
  buttons_[buttonCount_++] = {0, histY_, liveX - margin / 2, histH_, BUTTON_TIMELINE};
  const int icon = iconSize();
  buttons_[buttonCount_++] = {w_ - margin - icon, margin, icon, icon, BUTTON_WIFI};
}

// 表示の時刻 nowMs を右端、その HISTORY_SPAN_MS 前を左端とした x 座標。範囲外なら -1
int App::timelineX(int64_t ms, int64_t nowMs) const {
  if (nowMs < 0 || ms > nowMs || ms < nowMs - HISTORY_SPAN_MS) return -1;
  const int64_t t = ms - (nowMs - HISTORY_SPAN_MS);
  return axisX0_ + (int)((int64_t)(axisX1_ - axisX0_) * t / HISTORY_SPAN_MS);
}

int App::hitTimeline(int x) const {
  const int64_t now = platform_ ? platform_->displayTimeMs() : -1;
  const int limit = UI_HIST_HIT_PX * (h_ >= 480 ? 2 : 1);
  int best = -1, bestD = limit + 1;
  for (int i = 0; i < timeline_.count(); i++) {
    const int px = timelineX(timeline_.at(i).ms, now);
    if (px < 0) continue;
    const int d = abs(px - x);
    if (d < bestD || (d == bestD && best >= 0)) {  // 同じ距離なら新しい方
      best = i;
      bestD = d;
    }
  }
  return best;
}

int App::hitButton(int x, int y) const {
  const int slop = UI_TOUCH_SLOP;
  for (int i = 0; i < buttonCount_; i++) {
    const Button &b = buttons_[i];
    if (x >= b.x - slop && x < b.x + b.w + slop && y >= b.y - slop && y < b.y + b.h + slop) return i;
  }
  return -1;
}

void App::buttonAction(int index, int x) {
  if (index < 0 || index >= buttonCount_) return;
  const Button &b = buttons_[index];
  if (b.kind == BUTTON_LIVE) {
    goLive();
  } else if (b.kind == BUTTON_TIMELINE) {
    const int i = hitTimeline(x);
    if (i >= 0) selectHistory(timeline_.at(i).ms);
  } else {
    draft_ = wifi_;
    menuFocus_ = 0;
    screen_ = Screen::WIFI_MENU;
  }
}

void App::drawWifiIcon(g2::Graphics2D &g, int x, int y, int size, WifiState st) {
  uint32_t color = COLOR_TEXT;
  bool blinkOff = false;
  switch (st) {
    case WifiState::CONNECTED: color = platform_ && platform_->fetching() ? COLOR_WIFI_FETCHING : COLOR_WIFI_CONNECTED; break;
    case WifiState::CONNECTING: color = 0xFFFFD040u; blinkOff = fmodf(timeSec_, 1.0f) > 0.5f; break;
    case WifiState::FAILED: color = 0xFFFF5050u; break;
    case WifiState::NO_CONFIG: color = 0xFF808080u; break;
    case WifiState::UNAVAILABLE: color = 0xFF505050u; break;
  }
  // 中心の点と 2 本の弧 (上向きの扇形、角度 ±45°。角度は +x から時計回り)。弧はリングの扇形で描く
  const float cx = x + size / 2.0f, cy = y + size * 0.9f;
  const float thickness = size * 0.09f + 0.5f;
  g.fillCircle({cx, cy}, size * 0.08f + 0.5f, color);
  if (!blinkOff) {
    for (int a = 1; a < 3; a++) {
      const float r = size * (0.12f + 0.27f * a);
      g.fillCircleRingSector({cx, cy}, r, thickness, -3 * PI_F / 4, -PI_F / 4, color);
    }
  }
  if (st == WifiState::FAILED || st == WifiState::NO_CONFIG) {
    // 右下に × を重ねる
    const int s = size / 4, bx = x + size - s - 1, by = y + size - s - 1;
    g.drawLine(bx, by, bx + s, by + s, color);
    g.drawLine(bx + s, by, bx, by + s, color);
  }
}

// 左上の接続数「👁123」。目は 2 本の弧 (まぶた) と黒目の円で描く。高さは右上の WiFi アイコンの中央に揃える
void App::drawViewerCount(g2::Graphics2D &g, int count) {
  if (count < 0) return;
  const int scale = h_ >= 480 ? 2 : 1, margin = UI_MARGIN * scale;
  const int fh = uiFontH();
  const int icon = iconSize();
  const int y = margin + (icon > fh ? (icon - fh) / 2 : 0);

  // 目: 幅 eyeW、高さ 2 * sag のレンズ形。弧は半径 r の円の一部 (中心は目の中心から縦に r - sag)
  const float eyeW = fh * 1.3f, sag = fh * 0.42f;
  const float cx = margin + eyeW / 2, cy = y + fh / 2.0f;
  const float r = (eyeW * eyeW / 4 + sag * sag) / (2 * sag);
  const float half = asinf(eyeW / 2 / r);
  const float thickness = scale * 1.0f + 0.5f;
  g.fillCircleRingSector({cx, cy + (r - sag)}, r, thickness, -PI_F / 2 - half, -PI_F / 2 + half, COLOR_VIEWERS);
  g.fillCircleRingSector({cx, cy - (r - sag)}, r, thickness, PI_F / 2 - half, PI_F / 2 + half, COLOR_VIEWERS);
  g.fillCircle({cx, cy}, sag * 0.5f, COLOR_VIEWERS);
  char buf[12];
  snprintf(buf, sizeof buf, "%d", count);
  g.setTextColor(COLOR_VIEWERS);
  const g2::TextMetrics m = g.textMetrics(buf);
  g.drawString((int)(margin + eyeW + 2 * scale), y + (fh - m.height) / 2, buf);
}

// 3D の表示の右下隅に小さく版を出す (version.txt)
void App::drawVersion(g2::Graphics2D &g) {
#ifdef TKAPP_VERSION
  const int scale = h_ >= 480 ? 2 : 1, margin = UI_MARGIN * scale / 2;
  g.setFont(h_ >= 480 ? g2::Font(&g2::ShapoSansP_s12c09a01w02) : g2::Font(&g2::ShapoSansP_s08c07));
  static const char TEXT[] = "v" TKAPP_VERSION;
  const g2::TextMetrics m = g.textMetrics(TEXT);
  g.setTextColor(COLOR_VERSION);
  g.drawString(w_ - margin - m.width, viewH_ - margin - m.height, TEXT);
#else
  (void)g;
#endif
}

void App::drawMainUi(g2::Graphics2D &g) {
  drawVersion(g);
  g.setFont(uiFont());
  drawViewerCount(g, platform_ ? platform_->viewerCount() : -1);
  drawTimeline(g);
  for (int i = 0; i < buttonCount_; i++) {
    const Button &b = buttons_[i];
    const bool pressed = pressedButton_ == i;
    if (b.kind == BUTTON_LIVE) {
      // 左右端が半円のボタン。▶ (正三角形) と「LIVE」をまとめて横に中央寄せし、
      // 縦は▶の高さと「LIVE」の大文字の高さ (cap-height) をボタンの中央に揃える
      const int radius = b.h / 2;
      g.fillRoundRect(b.x, b.y, b.w, b.h, radius, pressed ? COLOR_KEY_PRESSED : COLOR_BUTTON_BG);
      g.drawRoundRect(b.x, b.y, b.w, b.h, radius, live_ ? COLOR_SELECTED_FRAME : COLOR_BUTTON_FRAME);
      const g2::TextMetrics m = g.textMetrics("LIVE");
      const int cap = capHeight(uiFont(), m.ascent);
      const float triH = b.h * 0.4f, triW = triH * (0.4f * 1.7320508f);  // 正三角形
      const float gap = b.h * 0.2f;
      const float x0 = b.x + (b.w - (triW + gap + m.width)) * 0.5f;
      const float cy = b.y + b.h * 0.5f;
      g.fillTriangle({x0, cy - triH / 2}, {x0, cy + triH / 2}, {x0 + triW, cy}, COLOR_TEXT);
      g.setTextColor(COLOR_TEXT);
      const int baseline = b.y + (b.h + cap + 1) / 2;
      g.drawString((int)(x0 + triW + gap + 0.5f), baseline - m.ascent, "LIVE");
    } else if (b.kind == BUTTON_WIFI) {
      drawWifiIcon(g, b.x, b.y, b.w, platform_ ? platform_->wifiState() : WifiState::UNAVAILABLE);
    }
  }
}

// 履歴の帯の時間軸: 横線と両端の目盛り、イベントの点 (停止 黄、発生 赤。発生を上に描く)。
// 選んだ点 (取得中を含む) は白い輪で囲む
void App::drawTimeline(g2::Graphics2D &g) {
  const int scale = h_ >= 480 ? 2 : 1;
  const int cy = histY_ + histH_ / 2, dotR = UI_HIST_DOT_R * scale, tick = UI_HIST_TICK * scale;
  g.fillRect(0, histY_, w_, histH_, 0xFF000000u);
  g.fillRect(axisX0_ - dotR, cy - (scale - 1) / 2, axisX1_ - axisX0_ + 2 * dotR, scale, COLOR_HIST_AXIS);
  g.fillRect(axisX0_ - dotR, cy - tick, scale, 2 * tick + 1, COLOR_HIST_AXIS);
  g.fillRect(axisX1_ + dotR - scale, cy - tick, scale, 2 * tick + 1, COLOR_HIST_AXIS);
  const int64_t now = platform_ ? platform_->displayTimeMs() : -1;
  if (now < 0) return;
  const int64_t sel = requestedMs_ ? requestedMs_ : (live_ ? 0 : selectedMs_);
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < timeline_.count(); i++) {
      const HistoryItem &it = timeline_.at(i);
      const bool inside = it.kind != LOG_STOPPING;
      if (inside != (pass == 1)) continue;
      const int x = timelineX(it.ms, now);
      if (x < 0) continue;
      if (it.ms == sel) g.fillCircle(x, cy, dotR + 2 * scale, COLOR_SELECTED_FRAME);
      g.fillCircle(x, cy, dotR, inside ? COLOR_KIND_INSIDE : COLOR_KIND_STOPPING);
    }
  }
}

void App::drawOverlay(g2::Graphics2D &g, int y) {
  g.resetTransform();
  g.translate(0, (float)-y);
  g.setBlend(g2::BlendMode::ALPHA, 255);
  if (screen_ == Screen::MAIN) {
    drawLabels(g, y);
    drawMainUi(g);
    drawLog(g);
  } else if (screen_ == Screen::WIFI_MENU) {
    drawWifiMenu(g);
  } else {
    drawKeyboard(g);
  }
  g.resetTransform();
}

// ---------------------------------------------------------------- イベントの記録

// 画面最下部に 1 列 1 時間、1 マス 10 分の画像を置く。列数は画面の幅を割り切れるもの (72 → 80 の順) を選び、
// 整数倍に拡大する (Tab5 の 360 幅は 72 列 × 5、CoreS3 の 240 幅は 80 列 × 3)。割り切れなければ 72 列で中央に置く
void App::layoutLog() {
  static const int CANDIDATES[] = {72, 80};
  logCols_ = CANDIDATES[0];
  for (int c : CANDIDATES) {
    if (c <= LOG_MAX_COLS && w_ % c == 0) {
      logCols_ = c;
      break;
    }
  }
  logScale_ = w_ / logCols_ > 0 ? w_ / logCols_ : 1;
  logH_ = logScale_ * LOG_SLOTS_PER_HOUR;
  logX_ = (w_ - logCols_ * logScale_) / 2;
  histH_ = UI_HIST_H * (h_ >= 480 ? 2 : 1);
  histY_ = h_ - logH_ - histH_;
  viewH_ = histY_;
  for (auto &p : logPixels_) p = 0;
  logTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, logCols_, LOG_SLOTS_PER_HOUR, logPixels_);
  hasLog_ = false;
}

void App::updateLog(const EventLog &log) {
  buildLogImage(log, logCols_, logPixels_);
  logNowSlot_ = log.nowSlot();
  hasLog_ = logNowSlot_ >= 0;
}

// 1 マス 1 ピクセルの画像をニアレストネイバーで拡大して描く (マスごとの fillRect より速い)
void App::drawLog(g2::Graphics2D &g) {
  const int logY = h_ - logH_;
  g.fillRect(0, logY, w_, logH_, 0xFF000000u);
  if (!hasLog_) return;
  const bool aa = g.antialias();
  g.setAntialias(false);
  g.drawImage(logTex_, g2::Rect{logX_, logY, logCols_ * logScale_, logH_});
  g.setAntialias(aa);
  // 現地時刻の 0, 6, 12, 18 時の境目に黒い縦線 (幅 1 px。その時の列の左端のピクセル)
  const int64_t hourNow = logNowSlot_ / LOG_SLOTS_PER_HOUR;
  for (int c = 1; c < logCols_; c++) {
    const int64_t hour = hourNow - (logCols_ - 1 - c) + LOG_TZ_HOURS;
    if (hour % LOG_GRID_HOURS == 0) g.fillRect(logX_ + c * logScale_, logY, 1, logH_, 0xFF000000u);
  }

}

// ---------------------------------------------------------------- WiFi 設定メニュー


void App::drawWifiMenu(g2::Graphics2D &g) {
  const int fh = uiFontH(), pad = fh / 2, margin = UI_MARGIN * (h_ >= 480 ? 2 : 1);
  g.setFont(uiFont());
  g.setTextColor(COLOR_TEXT);
  g.drawString(margin, margin, "WiFi settings");
  const int rowH = fh * 2 + pad * 2, top = margin + fh + pad * 2;
  for (int i = 0; i < FIELD_COUNT; i++) {
    const int y = top + rowH * i;
    const bool focus = menuFocus_ == i;
    g.setTextColor(COLOR_TEXT_DIM);
    g.drawString(margin, y, FIELD_LABELS[i]);
    g.fillRoundRect(margin, y + fh + 2, w_ - 2 * margin, fh + pad, 3, focus ? COLOR_FIELD_ACTIVE : COLOR_FIELD_BG);
    int maxLen;
    const char *v = fieldOf(draft_, i, maxLen);
    char shown[PASS_MAX + 1];
    if (i == 1) {
      const size_t n = strlen(v);
      for (size_t k = 0; k < n; k++) shown[k] = '*';
      shown[n] = 0;
    } else {
      strncpy(shown, v, sizeof shown - 1);
      shown[sizeof shown - 1] = 0;
    }
    g.setTextColor(COLOR_TEXT);
    g.drawString(margin + pad, y + fh + 2 + pad / 2, shown);
  }
  // ボタン
  const int by = top + rowH * FIELD_COUNT + pad, bw = (w_ - 3 * margin) / 2, bh = fh + pad * 2;
  const char *names[2] = {"Connect", "Cancel"};
  for (int i = 0; i < 2; i++) {
    const int bx = margin + (bw + margin) * i;
    const bool focus = menuFocus_ == MENU_CONNECT + i;
    g.fillRoundRect(bx, by, bw, bh, 4, focus ? COLOR_KEY_PRESSED : COLOR_KEY_BG);
    g.drawRoundRect(bx, by, bw, bh, 4, focus ? COLOR_SELECTED_FRAME : COLOR_BUTTON_FRAME);
    const g2::TextMetrics m = g.textMetrics(names[i]);
    g.setTextColor(COLOR_TEXT);
    g.drawString(bx + (bw - m.width) / 2, by + (bh - m.height) / 2, names[i]);
  }
}

void App::menuSelect(int item) {
  if (item < 0) return;
  if (item < FIELD_COUNT) {
    openKeyboard(item);
  } else if (item == MENU_CONNECT) {
    wifi_ = draft_;
    if (platform_) platform_->applyWifiConfig(wifi_);
    screen_ = Screen::MAIN;
  } else if (item == MENU_CANCEL) {
    screen_ = Screen::MAIN;
  }
}

// ---------------------------------------------------------------- キーボード

void App::openKeyboard(int field) {
  editField_ = field;
  int maxLen;
  const char *v = fieldOf(draft_, field, maxLen);
  editMax_ = maxLen;
  editLen_ = (int)strlen(v);
  memcpy(editBuf_, v, editLen_ + 1);
  kbdPage_ = 0;
  kbdFocus_ = 10;
  kbdPressed_ = -1;
  screen_ = Screen::KEYBOARD;
}

// キーの位置。idx 0..39 は文字キー、40.. は下段
static void keyRect(int idx, int w, int h, int fh, int &x, int &y, int &kw, int &kh) {
  const int margin = UI_MARGIN * (h >= 480 ? 2 : 1);
  const int unit = (w - 2 * margin) / KBD_COLS;
  kh = h >= 480 ? 48 : 30;
  const int rows = KBD_ROWS + 1;
  const int top = h - margin - rows * kh;
  (void)fh;
  if (idx < KBD_COLS * KBD_ROWS) {
    x = margin + unit * (idx % KBD_COLS);
    y = top + kh * (idx / KBD_COLS);
    kw = unit;
    return;
  }
  float fx = (float)margin;
  for (int i = 0; i < 6; i++) {
    const int kw2 = (int)(unit * KBD_BOTTOM_W[i]);
    if (KBD_BOTTOM_KEYS[i] == idx) {
      x = (int)fx;
      y = top + kh * KBD_ROWS;
      kw = kw2;
      return;
    }
    fx += kw2;
  }
  x = y = kw = 0;
}

void App::drawKeyboard(g2::Graphics2D &g) {
  const int fh = uiFontH(), pad = fh / 2, margin = UI_MARGIN * (h_ >= 480 ? 2 : 1);
  g.setFont(uiFont());
  g.setTextColor(COLOR_TEXT_DIM);
  g.drawString(margin, margin, FIELD_LABELS[editField_ < 0 ? 0 : editField_]);
  // 入力欄
  const int fy = margin + fh + pad;
  g.fillRoundRect(margin, fy, w_ - 2 * margin, fh + pad, 3, COLOR_FIELD_BG);
  g.drawRoundRect(margin, fy, w_ - 2 * margin, fh + pad, 3, COLOR_BUTTON_FRAME);
  g.setTextColor(COLOR_TEXT);
  // 長いときは末尾を見せる
  const char *shown = editBuf_;
  while (*shown && g.textMetrics(shown).width > w_ - 2 * margin - 2 * pad - fh / 2) shown++;
  g.drawString(margin + pad, fy + pad / 2, shown);
  const int cx = margin + pad + g.textMetrics(shown).width + 1;
  if (fmodf(timeSec_, 1.0f) < 0.6f) g.fillRect(cx, fy + pad / 2, 2, fh, COLOR_TEXT);

  char label[8];
  for (int idx = 0; idx < KEY_COUNT; idx++) {
    int x, y, kw, kh;
    keyRect(idx, w_, h_, fh, x, y, kw, kh);
    if (kw <= 0) continue;
    const char *text = label;
    if (idx < KBD_COLS * KBD_ROWS) {
      const char c = KBD_PAGES[kbdPage_][idx / KBD_COLS][idx % KBD_COLS];
      if (c == ' ') continue;
      label[0] = c;
      label[1] = 0;
    } else if (idx == KEY_SHIFT) {
      text = kbdPage_ == 1 ? "abc" : "ABC";
    } else if (idx == KEY_SYM) {
      text = kbdPage_ == 2 ? "abc" : "#+=";
    } else if (idx == KEY_SPACE) {
      text = "";
    } else if (idx == KEY_BKSP) {
      text = "<-";
    } else if (idx == KEY_OK) {
      text = "OK";
    } else {
      text = "Esc";
    }
    const bool pressed = kbdPressed_ == idx, focus = kbdFocus_ == idx;
    const uint32_t bg = pressed ? COLOR_KEY_PRESSED : (idx == KEY_OK ? 0xFF305070u : COLOR_KEY_BG);
    g.fillRoundRect(x + 1, y + 1, kw - 2, kh - 2, 3, bg);
    if (focus) g.drawRoundRect(x + 1, y + 1, kw - 2, kh - 2, 3, COLOR_SELECTED_FRAME);
    const g2::TextMetrics m = g.textMetrics(text);
    g.setTextColor(COLOR_TEXT);
    g.drawString(x + (kw - m.width) / 2, y + (kh - m.height) / 2, text);
  }
}

void App::keyboardInput(int idx) {
  if (idx < 0) return;
  if (idx < KBD_COLS * KBD_ROWS) {
    const char c = KBD_PAGES[kbdPage_][idx / KBD_COLS][idx % KBD_COLS];
    if (c == ' ') return;
    if (editLen_ < editMax_) {
      editBuf_[editLen_++] = c;
      editBuf_[editLen_] = 0;
    }
    if (kbdPage_ == 1) kbdPage_ = 0;  // 大文字は 1 文字だけ
  } else if (idx == KEY_SHIFT) {
    kbdPage_ = kbdPage_ == 1 ? 0 : 1;
  } else if (idx == KEY_SYM) {
    kbdPage_ = kbdPage_ == 2 ? 0 : 2;
  } else if (idx == KEY_SPACE) {
    if (editLen_ < editMax_) {
      editBuf_[editLen_++] = ' ';
      editBuf_[editLen_] = 0;
    }
  } else if (idx == KEY_BKSP) {
    if (editLen_ > 0) editBuf_[--editLen_] = 0;
  } else if (idx == KEY_OK) {
    int maxLen;
    char *v = fieldOf(draft_, editField_, maxLen);
    memcpy(v, editBuf_, editLen_ + 1);
    screen_ = Screen::WIFI_MENU;
  } else if (idx == KEY_CANCEL) {
    screen_ = Screen::WIFI_MENU;
  }
}

// ---------------------------------------------------------------- 設定画面の入力

// 画面の座標 → メニューの項目またはキー。無ければ -1
static int menuHit(int x, int y, int w, int h, int fh) {
  const int pad = fh / 2, margin = UI_MARGIN * (h >= 480 ? 2 : 1);
  const int rowH = fh * 2 + pad * 2, top = margin + fh + pad * 2;
  for (int i = 0; i < FIELD_COUNT; i++) {
    const int ry = top + rowH * i;
    if (y >= ry && y < ry + rowH && x >= margin && x < w - margin) return i;
  }
  const int by = top + rowH * FIELD_COUNT + pad, bw = (w - 3 * margin) / 2, bh = fh + pad * 2;
  for (int i = 0; i < 2; i++) {
    const int bx = margin + (bw + margin) * i;
    if (x >= bx && x < bx + bw && y >= by - UI_TOUCH_SLOP && y < by + bh + UI_TOUCH_SLOP) return MENU_CONNECT + i;
  }
  return -1;
}

static int keyHit(int px, int py, int w, int h, int fh) {
  for (int idx = 0; idx < KEY_COUNT; idx++) {
    int x, y, kw, kh;
    keyRect(idx, w, h, fh, x, y, kw, kh);
    if (kw <= 0) continue;
    if (px >= x && px < x + kw && py >= y && py < y + kh) return idx;
  }
  return -1;
}

void App::uiPointerDown(int x, int y) {
  if (screen_ == Screen::WIFI_MENU) {
    const int item = menuHit(x, y, w_, h_, uiFontH());
    if (item >= 0) menuFocus_ = item;
  } else if (screen_ == Screen::KEYBOARD) {
    kbdPressed_ = keyHit(x, y, w_, h_, uiFontH());
    if (kbdPressed_ >= 0) kbdFocus_ = kbdPressed_;
  }
}

void App::uiPointerUp(int x, int y) {
  if (screen_ == Screen::WIFI_MENU) {
    const int item = menuHit(x, y, w_, h_, uiFontH());
    if (item >= 0 && item == menuFocus_) menuSelect(item);
  } else if (screen_ == Screen::KEYBOARD) {
    const int idx = keyHit(x, y, w_, h_, uiFontH());
    if (idx >= 0 && idx == kbdPressed_) keyboardInput(idx);
    kbdPressed_ = -1;
  }
}

void App::uiKey(Key k) {
  if (screen_ == Screen::WIFI_MENU) {
    if (k == Key::UP) menuFocus_ = (menuFocus_ + MENU_ITEMS - 1) % MENU_ITEMS;
    else if (k == Key::DOWN) menuFocus_ = (menuFocus_ + 1) % MENU_ITEMS;
    else if (k == Key::LEFT || k == Key::RIGHT) {
      if (menuFocus_ >= MENU_CONNECT) menuFocus_ = menuFocus_ == MENU_CONNECT ? MENU_CANCEL : MENU_CONNECT;
    } else if (k == Key::A) menuSelect(menuFocus_);
    else if (k == Key::B) screen_ = Screen::MAIN;
    return;
  }
  if (screen_ == Screen::KEYBOARD) {
    if (k == Key::A) { keyboardInput(kbdFocus_); return; }
    if (k == Key::B) { screen_ = Screen::WIFI_MENU; return; }
    // フォーカスの移動。文字キーは格子、下段は横に並ぶ
    int f = kbdFocus_;
    if (f < KBD_COLS * KBD_ROWS) {
      int r = f / KBD_COLS, c = f % KBD_COLS;
      if (k == Key::LEFT) c = (c + KBD_COLS - 1) % KBD_COLS;
      else if (k == Key::RIGHT) c = (c + 1) % KBD_COLS;
      else if (k == Key::UP) r = r > 0 ? r - 1 : r;
      else if (k == Key::DOWN) {
        if (r + 1 < KBD_ROWS) r++;
        else { kbdFocus_ = c < 2 ? KEY_SHIFT : (c < 3 ? KEY_SYM : (c < 6 ? KEY_SPACE : (c < 8 ? KEY_BKSP : (c < 9 ? KEY_OK : KEY_CANCEL)))); return; }
      }
      kbdFocus_ = r * KBD_COLS + c;
    } else {
      int pos = 0;
      for (int i = 0; i < 6; i++) if (KBD_BOTTOM_KEYS[i] == f) pos = i;
      if (k == Key::LEFT) pos = (pos + 5) % 6;
      else if (k == Key::RIGHT) pos = (pos + 1) % 6;
      else if (k == Key::UP) { kbdFocus_ = (KBD_ROWS - 1) * KBD_COLS + pos * 2; return; }
      kbdFocus_ = KBD_BOTTOM_KEYS[pos];
    }
  }
}

}  // namespace tkapp
