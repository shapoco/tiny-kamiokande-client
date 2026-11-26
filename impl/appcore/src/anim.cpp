// 表示の切り替えと導入アニメーション (SPEC.md 3.4)。index.html の同名の関数の移植

#include <cmath>
#include <cstring>

#include "tkapp/app.hpp"

namespace tkapp {

namespace {

constexpr float PI_F = 3.14159265358979f;
constexpr float BOUNDING_RADIUS = 34.0132f;
constexpr float S_INFINITE = 1e9f;

inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float smoothstep01(float x) { return x * x * (3 - 2 * x); }
inline void copy3(const float *s, float *d) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
inline void along(const float *a, const float *u, float s, float *o) {
  for (int i = 0; i < 3; i++) o[i] = a[i] + u[i] * s;
}

// 始点 A から u の向きに延びる直線が円筒の内側にある区間。無ければ false
bool cylinderInterval(const float *A, const float *u, float &lo, float &hi) {
  lo = -1e30f;
  hi = 1e30f;
  if (fabsf(u[1]) > 1e-9f) {
    const float s1 = (-HEIGHT / 2 - A[1]) / u[1], s2 = (HEIGHT / 2 - A[1]) / u[1];
    lo = fmaxf(lo, fminf(s1, s2));
    hi = fminf(hi, fmaxf(s1, s2));
  } else if (fabsf(A[1]) > HEIGHT / 2) {
    return false;
  }
  const float a = u[0] * u[0] + u[2] * u[2], b = 2 * (A[0] * u[0] + A[2] * u[2]);
  const float c = A[0] * A[0] + A[2] * A[2] - RADIUS * RADIUS;
  if (a > 1e-9f) {
    const float disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    const float sq = sqrtf(disc);
    lo = fmaxf(lo, (-b - sq) / (2 * a));
    hi = fminf(hi, (-b + sq) / (2 * a));
  } else if (c > 0) {
    return false;
  }
  return lo < hi;
}

}  // namespace

void App::trackInterval(float &L, float &sMin, float &sMax, float &sIn, float &sOut) const {
  float d[3];
  for (int i = 0; i < 3; i++) d[i] = trackB_[i] - trackA_[i];
  L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  const bool through = trackKind_ == Kind::THROUGH;
  sMin = -BAND_EXTEND;
  sMax = through ? L + BAND_EXTEND : (trackKind_ == Kind::STOPPING ? trackStopS_ : 0);
  float lo, hi;
  if (!cylinderInterval(trackA_, trackU_, lo, hi)) {
    lo = 0;
    hi = L;
  }
  sIn = fminf(sMax, fmaxf(sMin, lo));
  sOut = fminf(sMax, fmaxf(sMin, hi));
}

float App::bandBrightness(float sPos, float L, float sMax) const {
  const float E = BAND_EXTEND, e = ENTRY_BRIGHTNESS;
  auto sq = [](float x) { return x * x; };
  if (trackKind_ == Kind::THROUGH) {
    if (sPos < 0) return e * sq(fmaxf(0, 1 + sPos / E));
    if (sPos <= L) return e + (1 - e) * sPos / L;
    return sq(fmaxf(0, 1 - (sPos - L) / E));
  }
  if (trackKind_ == Kind::STOPPING && sMax > 0) {
    if (sPos < 0) return e * sq(fmaxf(0, 1 + sPos / E));
    return e + (1 - e) * fminf(1, sPos / sMax);
  }
  return sq(fmaxf(0, 1 + fminf(0, sPos) / E));
}

float App::pinkNoise(float t) const {
  auto hash = [](float n) {
    const float v = sinf(n * 12.9898f + 78.233f) * 43758.5453f;
    return (v - floorf(v)) * 2 - 1;
  };
  auto valueNoise = [&](float x) {
    const float i = floorf(x), f = x - i, u = f * f * (3 - 2 * f);
    return hash(i) * (1 - u) + hash(i + 1) * u;
  };
  float v = 0, amp = 1, f = AURA_FLICKER_HZ, norm = 0;
  for (int k = 0; k < 6; k++) {
    v += amp * valueNoise(t * f + k * 17.3f);
    norm += amp;
    amp *= 0.70710678f;
    f *= 2;
  }
  return v / norm;
}

void App::setEventAura(float opacity, float sizeFactor) {
  auraOpacity_[1] = opacity;
  auraScale_[1] = AURA_SIZE_EVENT * sizeFactor;
  starOpacity_ = opacity;
  starScale_ = AURA_SIZE_EVENT * AURA_STAR_SIZE * sizeFactor;
}

void App::setTrack(const FitResult &fit) {
  trackKind_ = fit.kind;
  copy3(fit.start, trackA_);
  copy3(fit.exit, trackB_);
  float d[3];
  for (int i = 0; i < 3; i++) d[i] = fit.end[i] - fit.start[i];
  trackStopS_ = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  for (int i = 0; i < 3; i++) trackU_[i] = trackB_[i] - trackA_[i];
  const float l = sqrtf(trackU_[0] * trackU_[0] + trackU_[1] * trackU_[1] + trackU_[2] * trackU_[2]);
  if (l > 1e-9f) for (int i = 0; i < 3; i++) trackU_[i] /= l;
  float L, sMin, sMax, sIn, sOut;
  trackInterval(L, sMin, sMax, sIn, sOut);
  const bool through = trackKind_ == Kind::THROUGH;
  along(trackA_, trackU_, sIn, auraPos_[0]);
  copy3(AURA_COLOR_WALL, auraColor_[0]);
  along(trackA_, trackU_, through ? sOut : sMax, auraPos_[1]);
  copy3(through ? AURA_COLOR_WALL : AURA_COLOR_EVENT, auraColor_[1]);
  auraIsEvent_[0] = false;
  auraIsEvent_[1] = !through;
  starOpacity_ = 0;
}

void App::beginTransition(HistoryEntry *entry, bool instant) {
  if (!entry || !entry->rec) return;
  displayedId_ = entry->id;
  shownSec_ = 0;
  pendingCells_ = &entry->rec->data.cells;
  hasPending_ = true;
  introActive_ = false;
  headOpacity_ = 0;
  burst_ = BurstFx();
  burstAuraOpacity_ = burstCrossOpacity_ = burstRippleOpacity_ = 0;
  fitFadeTarget_ = 0;
  if (instant) {
    texBrightness_ = 0;
    fitFade_ = fitFadeEased_ = 0;
    bandOpacity_ = 0;
    auraOpacity_[0] = auraOpacity_[1] = 0;
    starOpacity_ = 0;
    fitVisible_ = false;
    detector_.setCells(pendingCells_);
    cellsId_ = displayedId_;
    hasPending_ = false;
    texturesDirty_ = true;
    texPhase_ = TexPhase::BLACK;
    maybeStartIntro();
  } else {
    texPhase_ = TexPhase::FADE_OUT;
  }
}

void App::maybeStartIntro() {
  if (texPhase_ != TexPhase::BLACK) return;
  HistoryEntry *e = findEntry(displayedId_);
  if (!e || !e->rec || !e->rec->fit.done) return;
  if (e->rec->fit.show) {
    startIntro();
  } else {
    revealRadius_ = -1;
    texturesDirty_ = true;
    texPhase_ = TexPhase::FADE_IN;
  }
}

void App::startIntro() {
  HistoryEntry *e = findEntry(displayedId_);
  if (!e || !e->rec) return;
  setTrack(e->rec->fit);
  const bool inside = isInsideKind(trackKind_);
  const bool through = trackKind_ == Kind::THROUGH;
  float L, sMin, sMax, sIn, sOut;
  trackInterval(L, sMin, sMax, sIn, sOut);
  const float v = METEOR_SPEED;
  intro_ = Intro();
  intro_.burst = inside;
  intro_.t = 0;
  intro_.sMin = sMin;
  intro_.sMax = sMax;
  intro_.sIn = sIn;
  intro_.sOut = sOut;
  intro_.v = v;
  intro_.revealEnd = (2 * BOUNDING_RADIUS + REVEAL_BLUR) / v;
  intro_.tIn = (sIn - sMin) / v;
  intro_.tEvent = ((through ? sOut : sMax) - sMin) / v;
  introActive_ = true;
  fitFade_ = fitFadeTarget_ = fitFadeEased_ = 1;
  bandShown_ = inside ? 0 : 1;
  bandOpacity_ = bandShown_;
  auraShown_[0] = auraShown_[1] = 0;
  auraOpacity_[0] = auraOpacity_[1] = 0;
  if (inside) {
    startBurst(trackA_, 1);
    copy3(trackA_, revealCenter_);
  }
  fitVisible_ = true;
  texBrightness_ = 1;
  revealRadius_ = 0;
  texPhase_ = TexPhase::INTRO;
}

void App::startBurst(const float *p, float strength) {
  burst_ = BurstFx();
  burst_.active = true;
  burst_.strength = strength;
  copy3(p, burst_.pos);
}

bool App::updateBurstFx(float dt) {
  if (!burst_.active) return false;
  burst_.t += dt;
  const float t = burst_.t, k = burst_.strength, bright = sqrtf(k);
  const float sz = AURA_SIZE_EVENT;
  {
    const float p = clamp01(t / BURST_AURA_SEC), e = 1 - (1 - p) * (1 - p) * (1 - p);
    const float start = AURA_SIZE * BURST_AURA_SCALE * k;
    burstAuraScale_ = start + (sz - start) * e;
    for (int i = 0; i < 3; i++) burstAuraColor_[i] = BURST_COLOR[i] + (AURA_COLOR_EVENT[i] - BURST_COLOR[i]) * e;
    const float handover = clamp01((p - 0.7f) / 0.3f);
    burstAuraOpacity_ = p < 1 ? (1 - handover) * bright : 0;
    setEventAura(handover, 1);
    if (p >= 1) auraShown_[1] = 1;
  }
  {
    const float p = clamp01(t / BURST_CROSS_SEC), e = 1 - (1 - p) * (1 - p);
    burstCrossScale_ = BURST_CROSS_SIZE * k * (1 - e) + 1;
    burstCrossAngle_ = BURST_CROSS_TURN * 2 * PI_F * e;
    burstCrossOpacity_ = p < 1 ? sqrtf(1 - p) * bright : 0;
  }
  {
    const float p = clamp01(t / BURST_RIPPLE_SEC), e = 1 - (1 - p) * (1 - p);
    burstRippleRadius_ = fmaxf(0.5f, BURST_RIPPLE_SIZE * k * e);
    burstRippleWidth_ = BURST_RIPPLE_WIDTH * (1 - 0.6f * e);
    burstRippleOpacity_ = p < 1 ? powf(1 - p, 1.5f) * bright : 0;
  }
  const float dur = fmaxf(BURST_AURA_SEC, fmaxf(BURST_CROSS_SEC, BURST_RIPPLE_SEC));
  if (t >= dur) burst_.done = true;
  return burst_.done;
}

void App::updateMeteor(float dt) {
  const Intro &it = intro_;
  const float t = it.t;
  const float sHead = it.sMin + it.v * t;
  const bool through = trackKind_ == Kind::THROUGH;

  const float ramp = BAND_EXTEND * 0.5f;
  float hb = clamp01((sHead - it.sMin) / ramp);
  hb *= through ? clamp01((it.sMax - sHead) / ramp) : clamp01(1 - (sHead - it.sMax) / (it.v * 0.15f));
  along(trackA_, trackU_, fminf(sHead, it.sMax), headPos_);
  headOpacity_ = hb;
  copy3(sHead >= it.sIn && sHead <= it.sOut ? METEOR_HEAD_COLOR_INSIDE : METEOR_HEAD_COLOR_OUTSIDE, headColor_);

  auto flash = [&](int i, float tHit) {
    if (t < tHit) return;
    auraShown_[i] = 1;
    const float p = clamp01((t - tHit) / FLASH_SEC);
    const float eased = 1 - (1 - p) * (1 - p);
    auraOpacity_[i] = 1;
    const float start = AURA_SIZE * FLASH_SCALE, end = auraIsEvent_[i] ? AURA_SIZE_EVENT : AURA_SIZE;
    auraScale_[i] = start + (end - start) * eased;
  };
  flash(0, it.tIn);
  if (through) {
    flash(1, it.tEvent);
  } else if (t >= it.tEvent) {
    if (!burst_.active) {
      float p[3];
      along(trackA_, trackU_, it.sMax, p);
      startBurst(p, STOP_BURST_STRENGTH);
    }
    updateBurstFx(dt);
  }

  if (t >= it.tIn) {
    along(trackA_, trackU_, it.sIn, revealCenter_);
    revealRadius_ = t - it.tIn >= it.revealEnd ? -1 : it.v * (t - it.tIn);
  }

  if (sHead >= it.sMax + it.v * FLASH_SEC && t - it.tIn >= it.revealEnd &&
      (through || (burst_.active && burst_.done))) {
    introActive_ = false;
    headOpacity_ = 0;
    burst_ = BurstFx();
    burstAuraOpacity_ = burstCrossOpacity_ = burstRippleOpacity_ = 0;
    auraShown_[0] = auraShown_[1] = 1;
    revealRadius_ = -1;
    texturesDirty_ = true;
    texPhase_ = TexPhase::SHOW;
  }
}

void App::updateBurstIntro(float dt) {
  const Intro &it = intro_;
  updateBurstFx(dt);
  revealRadius_ = it.t >= it.revealEnd ? -1 : it.v * it.t;
  if (burst_.done && it.t >= it.revealEnd) {
    introActive_ = false;
    burst_ = BurstFx();
    burstAuraOpacity_ = burstCrossOpacity_ = burstRippleOpacity_ = 0;
    auraShown_[0] = 0;
    auraShown_[1] = 1;
    setEventAura(1, 1);
    revealRadius_ = -1;
    texturesDirty_ = true;
    texPhase_ = TexPhase::SHOW;
  }
}

void App::updateIntro(float dt) {
  intro_.t += dt;
  if (intro_.burst) updateBurstIntro(dt);
  else updateMeteor(dt);
}

void App::updateTransition(float dt) {
  if (texPhase_ == TexPhase::FADE_OUT) {
    texBrightness_ = fmaxf(0, texBrightness_ - dt / TEXTURE_FADE_OUT_SEC);
    if (texBrightness_ == 0) {
      if (hasPending_) {
        detector_.setCells(pendingCells_);
        cellsId_ = displayedId_;
        hasPending_ = false;
        texturesDirty_ = true;
      }
      texPhase_ = TexPhase::BLACK;
      maybeStartIntro();
    }
  } else if (texPhase_ == TexPhase::FADE_IN) {
    texBrightness_ = fminf(1, texBrightness_ + dt / TEXTURE_FADE_IN_SEC);
    if (texBrightness_ == 1) {
      texPhase_ = TexPhase::SHOW;
      texturesDirty_ = true;
    }
  }

  if (introActive_) {
    updateIntro(dt);
    return;
  }
  if (fitFade_ != fitFadeTarget_) {
    const float step = dt / FADE_OUT_SEC;
    fitFade_ = fitFadeTarget_ > fitFade_ ? fminf(fitFadeTarget_, fitFade_ + step) : fmaxf(fitFadeTarget_, fitFade_ - step);
    fitFadeEased_ = smoothstep01(fitFade_);
    bandOpacity_ = fitFadeEased_ * bandShown_;
    if (fitFade_ == 0) fitVisible_ = false;
  }
  if (fitVisible_) {
    const float n = pinkNoise(timeSec_);
    for (int i = 0; i < 2; i++) {
      if (auraIsEvent_[i]) {
        setEventAura(fitFadeEased_ * auraShown_[i] * clamp01(1 + AURA_FLICKER * n), 1 + AURA_FLICKER_SIZE * n);
      } else {
        auraScale_[i] = AURA_SIZE * (1 + 0.08f * sinf(timeSec_ * 2.0f));
        auraOpacity_[i] = fitFadeEased_ * auraShown_[i];
      }
    }
  }
}

}  // namespace tkapp
