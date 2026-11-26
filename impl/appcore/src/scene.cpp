// 3D シーンの組み立て (検出器、方位、帯、オーラ、閃光) と画面上の文字

#include <cmath>
#include <cstring>

#include "tkapp/app.hpp"

namespace tkapp {

namespace {

constexpr float PI_F = 3.14159265358979f;
constexpr float S_INFINITE = 1e8f;

// 明るさ → RGB565_SWAPPED の灰色
inline uint16_t gray565(float v) {
  const int g = (int)((v < 0 ? 0 : (v > 1 ? 1 : v)) * 255 + 0.5f);
  const uint16_t p = (uint16_t)(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
  return (uint16_t)((p >> 8) | (p << 8));
}

inline g2::Color colorOf(const float *rgb, float m) {
  auto ch = [&](float c) {
    const float v = c * m * 255 + 0.5f;
    return v < 0 ? 0 : (v > 255 ? 255 : (int)v);
  };
  return g2::makeColor(ch(rgb[0]), ch(rgb[1]), ch(rgb[2]));
}

// 方位角 (北から時計回り、度) の水平方向の単位ベクトル (SPEC.md 3.2)
void azimuthToModel(float azimuthDeg, float *out) {
  const float phi = (NORTH_PHI_SK_DEG - azimuthDeg) * PI_F / 180.0f;
  out[0] = -sinf(phi);
  out[1] = 0;
  out[2] = -cosf(phi);
}

g3::Vertex vtx(float x, float y, float z, float u, float v, g2::Color c = g3::VERTEX_WHITE) {
  g3::Vertex o;
  o.position = {x, y, z};
  o.normal = {0, 1, 0};
  o.uv = {u, v};
  o.color = c;
  return o;
}

}  // namespace

void App::buildDetectorGeometry() {
  // 側面: 1 本のストリップ。角度 θ の列のテクスチャ u は (1 - θ/2π) × 150/256 (SPEC.md 3.1)
  // 頂点は行 (高さ) ごとに並べ、隣り合う 2 行で 1 本のストリップにする
  const float uScale = (float)SIDE_COLS / FACE_TEX_W, vBottom = (float)SIDE_ROWS / FACE_TEX_H;
  for (int d = 0; d <= SIDE_DIVS; d++) {
    const float t = (float)d / SIDE_DIVS;
    const float y = HEIGHT / 2 - HEIGHT * t;
    for (int s = 0; s <= SIDE_SEGMENTS; s++) {
      const float th = 2 * PI_F * s / SIDE_SEGMENTS;
      const float x = RADIUS * sinf(th), z = RADIUS * cosf(th);
      const float u = (1.0f - (float)s / SIDE_SEGMENTS) * uScale;
      sideVerts_[d * (SIDE_SEGMENTS + 1) + s] = vtx(x, y, z, u, vBottom * t);
    }
  }
  for (int d = 0; d < SIDE_DIVS; d++) {
    for (int s = 0; s <= SIDE_SEGMENTS; s++) {
      sideIdx_[d][2 * s] = (uint16_t)(d * (SIDE_SEGMENTS + 1) + s);
      sideIdx_[d][2 * s + 1] = (uint16_t)((d + 1) * (SIDE_SEGMENTS + 1) + s);
    }
  }
  // 上面・底面: 外接正方形 (三角形 2 枚)。テクスチャは側面の右の 48×48 の領域 (config.hpp) で、円の外は黒
  const float capScaleU = (float)CAP_COLS / FACE_TEX_W, capScaleV = (float)CAP_COLS / FACE_TEX_H;
  for (int f = 0; f < 2; f++) {
    const bool top = f == 0;
    const float y = top ? HEIGHT / 2 : -HEIGHT / 2;
    const float u0 = (float)(top ? CAP_TOP_TEX_X : CAP_BOTTOM_TEX_X) / FACE_TEX_W;
    const float xs[4] = {-RADIUS, RADIUS, RADIUS, -RADIUS}, zs[4] = {RADIUS, RADIUS, -RADIUS, -RADIUS};
    for (int i = 0; i < 4; i++) {
      const float u = u0 + (xs[i] / RADIUS + 1) * 0.5f * capScaleU;
      const float v = (top ? (1 - zs[i] / RADIUS) : (1 + zs[i] / RADIUS)) * 0.5f * capScaleV;
      capVerts_[f][i] = vtx(xs[i], y, zs[i], u, v);
    }
  }
  for (int i = 0; i < 128; i++) seqIdx_[i] = (uint16_t)i;
  // 上下面の縁の輪郭
  for (int f = 0; f < 2; f++) {
    const float y = f == 0 ? HEIGHT / 2 : -HEIGHT / 2;
    for (int s = 0; s < RIM_SEGMENTS; s++) {
      const float th = 2 * PI_F * s / RIM_SEGMENTS;
      rimVerts_[f][s] = vtx(RADIUS * sinf(th), y, RADIUS * cosf(th), 0, 0, RIM_COLOR);
    }
  }
  // 方位のリングと目盛り
  for (int i = 0; i < 90; i++) {
    float d[3];
    azimuthToModel(i * 4.0f, d);
    compassRing_[i] = vtx(d[0] * COMPASS_RADIUS, COMPASS_Y, d[2] * COMPASS_RADIUS, 0, 0, COMPASS_RING_COLOR);
  }
  int n = 0;
  for (int a = 0; a < 360; a += 10) {
    const float len = a % 90 == 0 ? 2.4f : (a % 45 == 0 ? 1.5f : 0.7f);
    float d[3];
    azimuthToModel((float)a, d);
    compassTicks_[n++] = vtx(d[0] * COMPASS_RADIUS, COMPASS_Y, d[2] * COMPASS_RADIUS, 0, 0, COMPASS_TICK_COLOR);
    compassTicks_[n++] = vtx(d[0] * (COMPASS_RADIUS + len), COMPASS_Y, d[2] * (COMPASS_RADIUS + len), 0, 0, COMPASS_TICK_COLOR);
  }
  for (int i = 0; i < FACILITY_COUNT; i++) {
    float d[3];
    azimuthToModel(FACILITIES[i].azimuthDeg, d);
    const float r0 = FACILITY_RADIUS, r1 = FACILITY_RADIUS + FACILITY_TICK;
    compassTicks_[n++] = vtx(d[0] * r0, COMPASS_Y, d[2] * r0, 0, 0, FACILITIES[i].color);
    compassTicks_[n++] = vtx(d[0] * r1, COMPASS_Y, d[2] * r1, 0, 0, FACILITIES[i].color);
  }
  for (int i = 0; i < 2 * BAND_ROWS; i++) bandIdx_[i] = (uint16_t)i;
}

void App::makeSpriteTextures() {
  // オーラ: 放射グラデーション (0: 1, 0.2: 0.7, 0.5: 0.22, 1: 0)
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      const float dx = (x + 0.5f) / 16 - 1, dy = (y + 0.5f) / 16 - 1;
      const float r = sqrtf(dx * dx + dy * dy);
      float a;
      if (r < 0.2f) a = 1 - (1 - 0.7f) * r / 0.2f;
      else if (r < 0.5f) a = 0.7f - (0.7f - 0.22f) * (r - 0.2f) / 0.3f;
      else if (r < 1) a = 0.22f * (1 - (r - 0.5f) / 0.5f);
      else a = 0;
      auraPixels_[y * 32 + x] = gray565(a);
    }
  }
  // 閃光の巨大なオーラと十字 (index.html の BURST_FRAG_*)。板の座標は -1..1
  auto arm = [](float px, float py, float width) {
    const float along = fabsf(px), across = fabsf(py);
    const float w = width * (1 - along) + 0.004f;
    return expf(-across * across / (w * w)) * powf(fmaxf(0, 1 - along), 1.5f);
  };
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      const float px = (x + 0.5f) / 32 - 1, py = (y + 0.5f) / 32 - 1;
      const float r = sqrtf(px * px + py * py);
      burstAuraPixels_[y * 64 + x] = gray565(0.55f * powf(fmaxf(0, 1 - r), 1.6f) + 0.8f * expf(-r * r * 20));
      const float qx = (px + py) * 0.7071f, qy = (px - py) * 0.7071f;
      const float core = 0.8f * expf(-(px * px + py * py) * 60);
      crossPixels_[y * 64 + x] =
          gray565(arm(px, py, 0.05f) + arm(py, px, 0.05f) + 0.35f * (arm(qx, qy, 0.05f) + arm(qy, qx, 0.05f)) + core);
      starPixels_[y * 64 + x] =
          gray565(arm(px, py, 0.04f) + arm(py, px, 0.04f) + 0.35f * (arm(qx, qy, 0.04f) + arm(qy, qx, 0.04f)) + core);
    }
  }
  // 帯の幅方向のグラデーション
  const float stops[][2] = {{0, 0}, {0.25f, 30 / 255.0f}, {0.4f, 150 / 255.0f}, {0.5f, 1}, {0.6f, 150 / 255.0f}, {0.75f, 30 / 255.0f}, {1, 0}};
  for (int x = 0; x < 128; x++) {
    const float u = (x + 0.5f) / 128;
    float v = 0;
    for (int i = 0; i + 1 < 7; i++) {
      if (u >= stops[i][0] && u <= stops[i + 1][0]) {
        v = stops[i][1] + (stops[i + 1][1] - stops[i][1]) * (u - stops[i][0]) / (stops[i + 1][0] - stops[i][0]);
      }
    }
    bandPixels_[x] = bandPixels_[128 + x] = gray565(v);
  }
  auraTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, 32, 32, auraPixels_);
  burstAuraTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, 64, 64, burstAuraPixels_);
  crossTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, 64, 64, crossPixels_);
  starTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, 64, 64, starPixels_);
  bandTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, 128, 2, bandPixels_);
}

void App::initScene() {
  buildDetectorGeometry();
  makeSpriteTextures();
  for (int i = 0; i < 3; i++) {
    matFace_[i] = g3::Material{};
    matFace_[i].diffuse = {1, 1, 1, 1};
    matFace_[i].texture = &detector_.texture();
    matFace_[i].blendMode = g2::BlendMode::ADD;
    matFace_[i].flags = g3::MaterialFlags::TEXTURE | g3::MaterialFlags::DOUBLE_SIDED;
  }
  matLine_ = g3::Material{};
  matLine_.diffuse = {1, 1, 1, 1};
  matLine_.blendMode = g2::BlendMode::ADD;
  matLine_.flags = g3::MaterialFlags::VERTEX_COLOR;
  matBand_ = g3::Material{};
  matBand_.diffuse = {1, 1, 1, 1};
  matBand_.texture = &bandTex_;
  matBand_.blendMode = g2::BlendMode::ADD;
  matBand_.flags = g3::MaterialFlags::TEXTURE | g3::MaterialFlags::DOUBLE_SIDED | g3::MaterialFlags::VERTEX_COLOR;
  for (auto &m : matSprite_) {
    m = g3::Material{};
    m.blendMode = g2::BlendMode::ADD;
    m.flags = g3::MaterialFlags::TEXTURE | g3::MaterialFlags::DOUBLE_SIDED;
  }
  matRipple_ = g3::Material{};
  matRipple_.blendMode = g2::BlendMode::ADD;
  matRipple_.flags = g3::MaterialFlags::VERTEX_COLOR | g3::MaterialFlags::DOUBLE_SIDED;
}

void App::putDetector() {
  g3::VertexBuffer vb = {(uint16_t)((SIDE_DIVS + 1) * (SIDE_SEGMENTS + 1)), sideVerts_};
  g3d_.setMaterial(matFace_[0]);
  for (int d = 0; d < SIDE_DIVS; d++) {
    g3::Primitive prim = {};
    prim.type = g3::PrimitiveType::TRIANGLE_STRIP;
    prim.indexCount = (uint16_t)(2 * (SIDE_SEGMENTS + 1));
    prim.vertexBuffer = &vb;
    prim.indices = sideIdx_[d];
    g3d_.putPrimitive(prim);
  }
  for (int f = 0; f < 2; f++) {
    g3::VertexBuffer cvb = {4, capVerts_[f]};
    g3::Primitive cp = {};
    cp.type = g3::PrimitiveType::TRIANGLE_FAN;
    cp.indexCount = 4;
    cp.vertexBuffer = &cvb;
    cp.indices = seqIdx_;
    g3d_.setMaterial(matFace_[1 + f]);
    g3d_.putPrimitive(cp);
  }
  g3d_.setMaterial(matLine_);
  for (int f = 0; f < 2; f++) {
    g3::VertexBuffer rvb = {(uint16_t)RIM_SEGMENTS, rimVerts_[f]};
    g3::Primitive rp = {};
    rp.type = g3::PrimitiveType::LINE_LOOP;
    rp.indexCount = (uint16_t)RIM_SEGMENTS;
    rp.vertexBuffer = &rvb;
    rp.indices = seqIdx_;
    g3d_.putPrimitive(rp);
  }
}

void App::putCompass() {
  g3d_.setMaterial(matLine_);
  {
    g3::VertexBuffer vb = {90, compassRing_};
    g3::Primitive p = {};
    p.type = g3::PrimitiveType::LINE_LOOP;
    p.indexCount = 90;
    p.vertexBuffer = &vb;
    p.indices = seqIdx_;
    g3d_.putPrimitive(p);
  }
  {
    const int n = 72 + 2 * FACILITY_COUNT;
    g3::VertexBuffer vb = {(uint16_t)n, compassTicks_};
    g3::Primitive p = {};
    p.type = g3::PrimitiveType::LINES;
    p.indexCount = (uint16_t)n;
    p.vertexBuffer = &vb;
    p.indices = seqIdx_;
    g3d_.putPrimitive(p);
  }
  // 文字の位置
  labelCount_ = 0;
  const char *names[4] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    float d[3];
    azimuthToModel(90.0f * i, d);
    const float r = COMPASS_RADIUS + COMPASS_LABEL_OFFSET;
    const float p[3] = {d[0] * r, COMPASS_Y, d[2] * r};
    Label &l = labels_[labelCount_++];
    l.text = names[i];
    l.color = i == 0 ? COMPASS_NORTH_COLOR : COMPASS_LABEL_COLOR;
    float depth;
    l.visible = project(p, l.sx, l.sy, depth);
  }
  for (int i = 0; i < FACILITY_COUNT; i++) {
    float d[3];
    azimuthToModel(FACILITIES[i].azimuthDeg, d);
    const float r = FACILITY_RADIUS + FACILITY_TICK + 6.0f;
    const float p[3] = {d[0] * r, COMPASS_Y, d[2] * r};
    Label &l = labels_[labelCount_++];
    l.text = FACILITIES[i].name;
    l.color = FACILITIES[i].color;
    float depth;
    l.visible = project(p, l.sx, l.sy, depth);
  }
}

void App::putBand(float sHead) {
  float L, sMin, sMax, sIn, sOut;
  trackInterval(L, sMin, sMax, sIn, sOut);
  // 断面: 等間隔 + 壁との交点 2 箇所 × 2 + 流星の頭 × 2。s の順に並べる
  struct Section {
    float s, k;
    bool inside;
  };
  Section sec[BAND_ROWS];
  int n = 0;
  const int nUniform = BAND_SECTIONS + 1;
  for (int i = 0; i < nUniform; i++) {
    const float s = sMin + (sMax - sMin) * i / (nUniform - 1);
    sec[n++] = {s, bandBrightness(s, L, sMax), s >= sIn && s <= sOut};
  }
  const float eps = 1e-4f;
  sec[n++] = {sIn - eps, bandBrightness(sIn, L, sMax), false};
  sec[n++] = {sIn + eps, bandBrightness(sIn, L, sMax), true};
  sec[n++] = {sOut - eps, bandBrightness(sOut, L, sMax), true};
  sec[n++] = {sOut + eps, bandBrightness(sOut, L, sMax), false};
  const float sH = fminf(sMax, fmaxf(sMin, sHead));
  const bool hIn = sH >= sIn && sH <= sOut;
  sec[n++] = {sH - eps, bandBrightness(sH, L, sMax), hIn};
  sec[n++] = {sH + eps, bandBrightness(sH, L, sMax), hIn};
  for (int i = 1; i < n; i++) {
    const Section t = sec[i];
    int j = i - 1;
    while (j >= 0 && sec[j].s > t.s) {
      sec[j + 1] = sec[j];
      j--;
    }
    sec[j + 1] = t;
  }

  // 幅の向き: 視線と軌跡の両方に直交する向き
  const float mid[3] = {(trackA_[0] + trackB_[0]) / 2, (trackA_[1] + trackB_[1]) / 2, (trackA_[2] + trackB_[2]) / 2};
  const float toCam[3] = {cam_.eye[0] - mid[0], cam_.eye[1] - mid[1], cam_.eye[2] - mid[2]};
  float side[3] = {toCam[1] * trackU_[2] - toCam[2] * trackU_[1], toCam[2] * trackU_[0] - toCam[0] * trackU_[2],
                   toCam[0] * trackU_[1] - toCam[1] * trackU_[0]};
  const float sl = side[0] * side[0] + side[1] * side[1] + side[2] * side[2];
  const bool canPlace = sl >= 1e-8f;
  if (canPlace) {
    const float inv = BAND_HALF_WIDTH / sqrtf(sl);
    for (int i = 0; i < 3; i++) side[i] *= inv;
  }
  const bool headed = sHead < S_INFINITE;
  for (int i = 0; i < n; i++) {
    const Section &s = sec[i];
    g3::Vertex &a = bandVerts_[2 * i], &b = bandVerts_[2 * i + 1];
    if (canPlace) {
      const float p[3] = {trackA_[0] + trackU_[0] * s.s, trackA_[1] + trackU_[1] * s.s, trackA_[2] + trackU_[2] * s.s};
      a.position = {p[0] - side[0], p[1] - side[1], p[2] - side[2]};
      b.position = {p[0] + side[0], p[1] + side[1], p[2] + side[2]};
    }
    const float v = (float)i / (n - 1);
    a.uv = {0, v};
    b.uv = {1, v};
    a.normal = b.normal = {0, 1, 0};
    float m = 0;
    if (s.s <= sHead) {
      m = s.k;
      if (headed) m += METEOR_TAIL_BOOST * expf(-(sHead - s.s) / METEOR_TAIL_LEN);
    }
    const g2::Color c = colorOf(s.inside ? BAND_COLOR : BAND_COLOR_OUTSIDE, m * BAND_BRIGHTNESS_GAIN);
    a.color = b.color = c;
  }
  matBand_.diffuse = {1, 1, 1, bandOpacity_};
  g3::VertexBuffer vb = {(uint16_t)(2 * n), bandVerts_};
  g3::Primitive p = {};
  p.type = g3::PrimitiveType::TRIANGLE_STRIP;
  p.indexCount = (uint16_t)(2 * n);
  p.vertexBuffer = &vb;
  p.indices = bandIdx_;
  g3d_.setMaterial(matBand_);
  g3d_.putPrimitive(p);
}

void App::putSprite(const float *pos, float size, const float *rgb, float opacity, const g2::Texture *tex, float angle) {
  if (spriteMatUsed_ >= (int)(sizeof(matSprite_) / sizeof(matSprite_[0]))) return;
  const float half = size / 2, c = cosf(angle), s = sinf(angle);
  float r[3], u[3];
  for (int i = 0; i < 3; i++) {
    r[i] = (cam_.right[i] * c + cam_.up[i] * s) * half;
    u[i] = (-cam_.right[i] * s + cam_.up[i] * c) * half;
  }
  quadVerts_[0] = vtx(pos[0] - r[0] + u[0], pos[1] - r[1] + u[1], pos[2] - r[2] + u[2], 0, 0);
  quadVerts_[1] = vtx(pos[0] + r[0] + u[0], pos[1] + r[1] + u[1], pos[2] + r[2] + u[2], 1, 0);
  quadVerts_[2] = vtx(pos[0] + r[0] - u[0], pos[1] + r[1] - u[1], pos[2] + r[2] - u[2], 1, 1);
  quadVerts_[3] = vtx(pos[0] - r[0] - u[0], pos[1] - r[1] - u[1], pos[2] - r[2] - u[2], 0, 1);
  g3::Material &m = matSprite_[spriteMatUsed_++];
  m.diffuse = {rgb[0], rgb[1], rgb[2], opacity > 1 ? 1 : opacity};
  m.texture = tex;
  g3::VertexBuffer vb = {4, quadVerts_};
  g3::Primitive p = {};
  p.type = g3::PrimitiveType::TRIANGLE_FAN;
  p.indexCount = 4;
  p.vertexBuffer = &vb;
  p.indices = seqIdx_;
  g3d_.setMaterial(m);
  g3d_.putPrimitive(p);
}

void App::putRipple(const float *pos, float radius, float width, const float *rgb, float opacity) {
  constexpr int SEG = 32;
  const g2::Color bright = colorOf(rgb, 1), dark = g2::makeColor(0, 0, 0);
  for (int i = 0; i <= SEG; i++) {
    const float a = 2 * PI_F * i / SEG, c = cosf(a), s = sinf(a);
    for (int ring = 0; ring < 3; ring++) {
      const float rr = radius + (ring - 1) * width;
      g3::Vertex &v = rippleVerts_[ring * (SEG + 1) + i];
      v.position = {pos[0] + (cam_.right[0] * c + cam_.up[0] * s) * rr, pos[1] + (cam_.right[1] * c + cam_.up[1] * s) * rr,
                    pos[2] + (cam_.right[2] * c + cam_.up[2] * s) * rr};
      v.normal = {0, 1, 0};
      v.uv = {0, 0};
      v.color = ring == 1 ? bright : dark;
    }
  }
  int n = 0;
  for (int i = 0; i <= SEG; i++) {
    rippleIdx_[n++] = (uint16_t)i;
    rippleIdx_[n++] = (uint16_t)(SEG + 1 + i);
  }
  const int nA = n;
  for (int i = 0; i <= SEG; i++) {
    rippleIdx_[n++] = (uint16_t)(SEG + 1 + i);
    rippleIdx_[n++] = (uint16_t)(2 * (SEG + 1) + i);
  }
  matRipple_.diffuse = {1, 1, 1, opacity > 1 ? 1 : opacity};
  g3::VertexBuffer vb = {(uint16_t)(3 * (SEG + 1)), rippleVerts_};
  g3d_.setMaterial(matRipple_);
  g3::Primitive p = {};
  p.type = g3::PrimitiveType::TRIANGLE_STRIP;
  p.indexCount = (uint16_t)nA;
  p.vertexBuffer = &vb;
  p.indices = rippleIdx_;
  g3d_.putPrimitive(p);
  p.indices = rippleIdx_ + nA;
  p.indexCount = (uint16_t)(n - nA);
  g3d_.putPrimitive(p);
}

void App::drawLabels(g2::Graphics2D &g, int y) {
  (void)y;
  g.setFont(uiFont());
  for (int i = 0; i < labelCount_; i++) {
    const Label &l = labels_[i];
    if (!l.visible) continue;
    const g2::TextMetrics m = g.textMetrics(l.text);
    g.setTextColor(l.color);
    g.drawString((int)(l.sx - m.width / 2.0f), (int)(l.sy - m.height / 2.0f), l.text);
  }
}

}  // namespace tkapp
