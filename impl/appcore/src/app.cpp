#include "tkapp/app.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tkapp {

namespace {

constexpr float PI_F = 3.14159265358979f;
constexpr float BOUNDING_RADIUS = 34.0132f;  // hypot(RADIUS, HEIGHT / 2)

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float len3(const float *a) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
inline float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void cross3(const float *a, const float *b, float *o) {
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}
inline void norm3(float *a) {
  const float l = len3(a);
  if (l > 1e-12f) {
    a[0] /= l;
    a[1] /= l;
    a[2] /= l;
  }
}

}  // namespace

bool App::init(int w, int h, const Buffers &buffers, Platform *platform, void *arena, size_t arenaBytes,
               int renderContexts) {
  w_ = w;
  h_ = h;
  platform_ = platform;
  buffers_ = buffers;
  if (!buffers.texturePixels || !buffers.records || buffers.recordCapacity < RECORD_SLOTS) return false;
  layoutLog();  // viewH_ を決める
  g3::Config cfg = g3::defaultConfig((int16_t)w, (int16_t)viewH_, arena, arenaBytes);
  cfg.renderContexts = renderContexts;
  cfg.spanCapacity = SPAN_CAPACITY;
  g3d_.init(cfg);
  if (!g3d_.isInitialized()) return false;
  detector_.init(buffers.texturePixels);
  detector_.setCells(nullptr);
  for (int i = 0; i < RECORD_SLOTS; i++) records_[i] = {0, &buffers.records[i]};
  displayedId_ = cellsId_ = latestLiveId_ = pendingLiveId_ = 0;
  initCamera();
  initScene();
  layoutButtons();
  texPhase_ = TexPhase::BLACK;
  texBrightness_ = 0;
  revealRadius_ = -1;
  texturesDirty_ = true;
  return true;
}

// ---------------------------------------------------------------- カメラ

void App::initCamera() {
  const float d[3] = {RADIUS * 1.8f, HEIGHT * 0.7f, -RADIUS * 2.6f};
  const float l = len3(d);
  cam_.yaw = cam_.yawT = atan2f(d[0], d[2]);
  cam_.pitch = cam_.pitchT = asinf(d[1] / l);
  cam_.aspect = (float)w_ / (float)viewH_;
  cam_.f = 1.0f / tanf(CAMERA_FOV_DEG * PI_F / 360.0f);
  cam_.dist = cam_.distT = cam_.fittedDist = fitCameraDistance();
  cam_.idleSec = 0;
  cam_.autoRotateSpeed = 0;
  updateCamera(0, false);
}

float App::fitCameraDistance() const {
  const float halfFovV = CAMERA_FOV_DEG * PI_F / 360.0f;
  const float halfFovMin = atanf(tanf(halfFovV) * (cam_.aspect < 1 ? cam_.aspect : 1.0f));
  return BOUNDING_RADIUS * FIT_MARGIN / sinf(halfFovMin);
}

void App::updateCamera(float dt, bool interacting) {
  if (keyHeld_[(int)Key::LEFT]) cam_.yawT += KEY_ROTATE_SPEED * dt;
  if (keyHeld_[(int)Key::RIGHT]) cam_.yawT -= KEY_ROTATE_SPEED * dt;
  if (keyHeld_[(int)Key::UP]) cam_.distT *= expf(-KEY_ZOOM_RATE * dt);
  if (keyHeld_[(int)Key::DOWN]) cam_.distT *= expf(KEY_ZOOM_RATE * dt);
  cam_.distT = clampf(cam_.distT, CAMERA_MIN_DIST, CAMERA_MAX_DIST);
  cam_.pitchT = clampf(cam_.pitchT, -PITCH_LIMIT, PITCH_LIMIT);

  if (interacting) {
    cam_.idleSec = 0;
    cam_.autoRotateSpeed = 0;
  } else {
    cam_.idleSec += dt;
    if (cam_.idleSec >= IDLE_BEFORE_AUTO_ROTATE_SEC) {
      cam_.autoRotateSpeed += AUTO_ROTATE_SPEED * dt / AUTO_ROTATE_RAMP_SEC;
      if (cam_.autoRotateSpeed > AUTO_ROTATE_SPEED) cam_.autoRotateSpeed = AUTO_ROTATE_SPEED;
      cam_.yawT += cam_.autoRotateSpeed * dt;
    }
  }
  // ダンピング (OrbitControls と同じく 1/60 秒ごとに dampingFactor の割合で近づく)
  const float k = dt > 0 ? 1 - powf(1 - CAMERA_DAMPING, dt * 60.0f) : 1;
  cam_.yaw += (cam_.yawT - cam_.yaw) * k;
  cam_.pitch += (cam_.pitchT - cam_.pitch) * k;
  cam_.dist += (cam_.distT - cam_.dist) * k;
  // 角度を巻き戻して値が大きくならないようにする
  if (cam_.yaw > PI_F) { cam_.yaw -= 2 * PI_F; cam_.yawT -= 2 * PI_F; }
  if (cam_.yaw < -PI_F) { cam_.yaw += 2 * PI_F; cam_.yawT += 2 * PI_F; }

  const float cp = cosf(cam_.pitch);
  cam_.eye[0] = cam_.dist * sinf(cam_.yaw) * cp;
  cam_.eye[1] = cam_.dist * sinf(cam_.pitch);
  cam_.eye[2] = cam_.dist * cosf(cam_.yaw) * cp;
  for (int i = 0; i < 3; i++) cam_.fwd[i] = -cam_.eye[i];
  norm3(cam_.fwd);
  const float up0[3] = {0, 1, 0};
  cross3(cam_.fwd, up0, cam_.right);
  norm3(cam_.right);
  cross3(cam_.right, cam_.fwd, cam_.up);
}

bool App::project(const float p[3], float &sx, float &sy, float &depth) const {
  const float v[3] = {p[0] - cam_.eye[0], p[1] - cam_.eye[1], p[2] - cam_.eye[2]};
  const float zv = dot3(v, cam_.fwd);
  if (zv < CAMERA_NEAR) return false;
  const float xv = dot3(v, cam_.right), yv = dot3(v, cam_.up);
  sx = (xv * cam_.f / cam_.aspect / zv + 1) * 0.5f * w_;
  sy = (1 - yv * cam_.f / zv) * 0.5f * viewH_;
  depth = zv;
  return true;
}

// ---------------------------------------------------------------- 入力

void App::pointerDown(int id, int x, int y) {
  if (id < 0 || id >= MAX_POINTERS) return;
  Pointer &p = ptr_[id];
  p.down = true;
  p.x = p.x0 = x;
  p.y = p.y0 = y;
  p.sec = 0;
  p.moved = false;
  p.button = -1;
  p.secondTap = false;
  p.holding = false;
  interacted_ = true;
  if (screen_ != Screen::MAIN) {
    uiPointerDown(x, y);
    return;
  }
  p.button = hitButton(x, y);
  pressedButton_ = p.button;
  if (!multiTouch_ && p.button < 0 && timeSec_ - lastTapSec_ <= DOUBLE_TAP_SEC &&
      abs(x - lastTapX_) <= DOUBLE_TAP_MAX_DIST_PX && abs(y - lastTapY_) <= DOUBLE_TAP_MAX_DIST_PX) {
    p.secondTap = true;
  }
  const Pointer &o = ptr_[id ^ 1];
  if (o.down && o.button < 0 && p.button < 0) {
    const int dx = o.x - x, dy = o.y - y;
    pinchDist0_ = (int)sqrtf((float)(dx * dx + dy * dy));
    pinchCamDist0_ = cam_.distT;
  }
}

void App::pointerMove(int id, int x, int y) {
  if (id < 0 || id >= MAX_POINTERS) return;
  Pointer &p = ptr_[id];
  if (!p.down) return;
  const int dx = x - p.x, dy = y - p.y;
  p.x = x;
  p.y = y;
  if (abs(x - p.x0) > TAP_MAX_MOVE_PX || abs(y - p.y0) > TAP_MAX_MOVE_PX) p.moved = true;
  interacted_ = true;
  if (screen_ != Screen::MAIN) return;
  if (p.button >= 0) {
    if (!p.moved) return;
    p.button = -1;  // 押したまま動かしたら押下を取り消す
    pressedButton_ = -1;
  }
  const Pointer &o = ptr_[id ^ 1];
  if (o.down && o.button < 0) {
    if (!multiTouch_) return;
    const int ex = o.x - x, ey = o.y - y;
    const float d = sqrtf((float)(ex * ex + ey * ey));
    if (pinchDist0_ > 0 && d > 1) cam_.distT = clampf(pinchCamDist0_ * pinchDist0_ / d, CAMERA_MIN_DIST, CAMERA_MAX_DIST);
    return;
  }
  const float k = 2 * PI_F / h_;
  cam_.yawT -= dx * k;
  cam_.pitchT = clampf(cam_.pitchT + dy * k, -PITCH_LIMIT, PITCH_LIMIT);
}

void App::pointerUp(int id) {
  if (id < 0 || id >= MAX_POINTERS) return;
  Pointer &p = ptr_[id];
  if (!p.down) return;
  p.down = false;
  interacted_ = true;
  if (screen_ != Screen::MAIN) {
    uiPointerUp(p.x, p.y);
    return;
  }
  if (p.button >= 0 && !p.moved && p.sec < TAP_MAX_SEC && hitButton(p.x, p.y) == p.button) buttonAction(p.button, p.x);
  // シングルタッチのズーム: 3D の表示のダブルタップで拡大 (長押しの縮小は advance())
  if (!multiTouch_ && p.button < 0 && !p.moved && !p.holding && p.sec < TAP_MAX_SEC) {
    if (p.secondTap) {
      cam_.distT = clampf(cam_.distT * DOUBLE_TAP_ZOOM, CAMERA_MIN_DIST, CAMERA_MAX_DIST);
      lastTapSec_ = -1e9f;  // 3 回目は新しい 1 回目
    } else {
      lastTapSec_ = timeSec_;
      lastTapX_ = p.x;
      lastTapY_ = p.y;
    }
  }
  p.button = -1;
  pressedButton_ = -1;
  pinchDist0_ = 0;
}

void App::keyDown(Key k) {
  if ((int)k >= (int)Key::COUNT) return;
  keyHeld_[(int)k] = true;
  interacted_ = true;
  if (screen_ != Screen::MAIN) {
    uiKey(k);
    return;
  }
  if (k == Key::A) {
    // 履歴を新しい方から順に送り、最後まで行ったら LIVE へ戻る
    const int64_t cur = requestedMs_ ? requestedMs_ : selectedMs_;
    for (int i = timeline_.count() - 1; i >= 0; i--) {
      const int64_t ms = timeline_.at(i).ms;
      if (cur == 0 || ms < cur) {
        if (timelineX(ms, platform_ ? platform_->displayTimeMs() : -1) >= 0) {
          selectHistory(ms);
          return;
        }
        break;
      }
    }
    goLive();
  } else if (k == Key::B) {
    draft_ = wifi_;
    menuFocus_ = 0;
    screen_ = Screen::WIFI_MENU;
  }
}

void App::keyUp(Key k) {
  if ((int)k < (int)Key::COUNT) keyHeld_[(int)k] = false;
}

// ---------------------------------------------------------------- イベントの実体と LIVE / 履歴

HistoryEntry *App::findEntry(uint32_t id) {
  if (id == 0) return nullptr;
  for (auto &e : records_) if (e.id == id) return &e;
  return nullptr;
}

HistoryEntry *App::allocEntry(bool forLive) {
  for (auto &e : records_) {
    if (e.id == 0 || (e.id != displayedId_ && e.id != cellsId_ && e.id != latestLiveId_)) return &e;
  }
  if (forLive) {
    HistoryEntry *e = findEntry(latestLiveId_);
    if (e && e->id != displayedId_ && e->id != cellsId_) return e;
  }
  return nullptr;
}

void App::pushEvent(const EventRecord &rec) {
  HistoryEntry *e = allocEntry(true);
  if (!e) return;
  memcpy(e->rec, &rec, sizeof(EventRecord));
  e->id = nextId_++;
  latestLiveId_ = e->id;
  if (live_) {
    if (displayedId_ == 0) beginTransition(e, true);
    else if (shownSec_ >= SHOW_MIN_SEC) beginTransition(e, false);
    else pendingLiveId_ = e->id;
  }
  if (platform_) {
    char buf[96];
    snprintf(buf, sizeof buf, "event %lu: lit %u, od %u, %s corr %.3f hide %d, fit %lu ms",
             (unsigned long)e->id, rec.data.litCount, rec.data.odCount,
             rec.fit.show ? "show" : "hidden", (double)rec.fit.corr, rec.fit.hideReason,
             (unsigned long)rec.fit.fitMs);
    platform_->log(buf);
  }
}

void App::updateHistory(const HistoryList &list) { timeline_ = list; }

// 時間軸の点を選んだ。内容をプラットフォームに頼み、届いたら showHistoryEvent() で再生する
void App::selectHistory(int64_t ms) {
  if (ms == selectedMs_ && !live_ && requestedMs_ == 0) return;
  requestedMs_ = ms;
  if (platform_) platform_->requestHistory(ms);
}

void App::showHistoryEvent(const EventRecord &rec, int64_t ms) {
  if (ms != requestedMs_) return;  // 取得中に別の点か LIVE を押した
  requestedMs_ = 0;
  HistoryEntry *e = allocEntry(false);
  if (!e) return;
  memcpy(e->rec, &rec, sizeof(EventRecord));
  e->id = nextId_++;
  live_ = false;
  pendingLiveId_ = 0;
  selectedMs_ = ms;
  beginTransition(e, true);
  if (platform_) {
    char buf[64];
    snprintf(buf, sizeof buf, "history %lld: lit %u", (long long)ms, rec.data.litCount);
    platform_->log(buf);
  }
}

void App::historyFailed(int64_t ms) {
  if (ms == requestedMs_) requestedMs_ = 0;
}

// LIVE に戻り、最新の LIVE のイベントを即座に再生する
void App::goLive() {
  requestedMs_ = 0;
  if (live_) return;
  live_ = true;
  selectedMs_ = 0;
  pendingLiveId_ = 0;
  HistoryEntry *e = findEntry(latestLiveId_);
  if (e) beginTransition(e, true);
}

// ---------------------------------------------------------------- フレーム

void App::advance(float dt) {
  if (dt < 0) dt = 0;
  if (dt > 0.25f) dt = 0.25f;
  timeSec_ += dt;
  bool pointerActive = false;
  for (auto &p : ptr_) {
    if (p.down) p.sec += dt;
    if (p.down && p.button < 0) pointerActive = true;
    // シングルタッチのズーム: 3D の表示を動かさずに押し続けると縮小
    if (!multiTouch_ && screen_ == Screen::MAIN && p.down && p.button < 0 && !p.moved && p.sec >= HOLD_ZOOM_SEC) {
      p.holding = true;
      cam_.distT = clampf(cam_.distT * expf(HOLD_ZOOM_RATE * dt), CAMERA_MIN_DIST, CAMERA_MAX_DIST);
    }
  }
  const bool rotating = keyHeld_[(int)Key::LEFT] || keyHeld_[(int)Key::RIGHT] || keyHeld_[(int)Key::UP] ||
                        keyHeld_[(int)Key::DOWN];
  updateCamera(dt, interacted_ || pointerActive || rotating);
  interacted_ = false;

  shownSec_ += dt;
  if (live_ && pendingLiveId_ != 0 && shownSec_ >= SHOW_MIN_SEC) {
    HistoryEntry *e = findEntry(pendingLiveId_);
    pendingLiveId_ = 0;
    if (e) beginTransition(e, false);
  }
  updateTransition(dt);

  if (texturesDirty_ || revealRadius_ >= 0 || texPhase_ == TexPhase::FADE_OUT || texPhase_ == TexPhase::FADE_IN) {
    detector_.updateTextures(texBrightness_, revealCenter_, revealRadius_, REVEAL_BLUR);
    texturesDirty_ = false;
  }

  fpsAccum_ += dt;
  fpsFrames_++;
  if (fpsAccum_ >= 1.0f) {
    stats_.fps = fpsFrames_ / fpsAccum_;
    fpsAccum_ = 0;
    fpsFrames_ = 0;
  }
  stats_.frames++;
}

void App::buildScene() {
  sceneBuilt_ = screen_ == Screen::MAIN;
  if (!sceneBuilt_) return;
  g3d_.beginScene();
  g3d_.loadIdentity();
  g3d_.lookAt({cam_.eye[0], cam_.eye[1], cam_.eye[2]}, {0, 0, 0}, {0, 1, 0});
  g3d_.setPerspectiveProjection(CAMERA_FOV_DEG * PI_F / 180.0f, cam_.aspect, CAMERA_NEAR, CAMERA_FAR);
  g3d_.setClearColor({0, 0, 0, 1});
  g3d_.disableParallelLight();
  g3d_.disableEnvironmentLight();
  g3d_.beginLayer(g3::LayerFlags::NO_DEPTH);
  spriteMatUsed_ = 0;
  labelCount_ = 0;
  putDetector();
  putCompass();
  if (fitVisible_) {
    if (bandShown_ > 0 && bandOpacity_ > 0.002f) putBand(introActive_ && !intro_.burst ? intro_.sMin + intro_.v * intro_.t : 1e9f);
    for (int i = 0; i < 2; i++) {
      if (auraOpacity_[i] > 0.002f) putSprite(auraPos_[i], auraScale_[i], auraColor_[i], auraOpacity_[i], &auraTex_);
    }
    if (headOpacity_ > 0.002f) putSprite(headPos_, METEOR_HEAD_SIZE, headColor_, headOpacity_, &auraTex_);
    if (burst_.active) {
      if (burstAuraOpacity_ > 0.002f) putSprite(burst_.pos, burstAuraScale_, burstAuraColor_, burstAuraOpacity_, &burstAuraTex_);
      if (burstCrossOpacity_ > 0.002f) putSprite(burst_.pos, burstCrossScale_, BURST_COLOR, burstCrossOpacity_, &crossTex_, burstCrossAngle_);
      if (burstRippleOpacity_ > 0.002f) putRipple(burst_.pos, burstRippleRadius_, burstRippleWidth_, BURST_COLOR, burstRippleOpacity_);
    }
    if (starOpacity_ > 0.002f) putSprite(auraPos_[1], starScale_, AURA_STAR_COLOR, starOpacity_, &starTex_);
  }
  g3d_.endScene();
  g3d_.beginRender();
}

void App::renderStrip(int ctx, g2::Graphics2D &g, const g2::Surface &target, int y) {
  // 3D は最下部の記録の上まで (記録の行は drawLog が塗る)
  if (sceneBuilt_ && y < viewH_) {
    const int rows = viewH_ - y < target.height ? viewH_ - y : target.height;
    g3d_.render(ctx, 0, (int16_t)y, (int16_t)w_, (int16_t)rows, target);
  }

  g.setTarget(target);
  g.resetClipRect();
  g.setBlend(g2::BlendMode::ALPHA, 255);
  if (!sceneBuilt_) g.clear(COLOR_PANEL_BG);
  drawOverlay(g, y);
}

void App::endFrame() {
  if (!sceneBuilt_) return;
  g3d_.endRender();
  const g3::Stats s = g3d_.getStats();
  stats_.triCount = (uint32_t)s.triCount;
  stats_.spanPeak = (uint32_t)s.spanPeak;
  stats_.triBytes = (uint32_t)s.triBytes;
}

}  // namespace tkapp
