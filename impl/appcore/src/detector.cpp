#include "tkapp/detector.hpp"

#include <cmath>
#include <cstring>

namespace tkapp {

namespace g2 = shapoco::gfx2d;

namespace {

constexpr float PI_F = 3.14159265358979f;

// 凡例の色 (cbor.cpp の PALETTE_RGB と同じ)
const uint8_t LEGEND_RGB[CELL_PALETTE_COUNT][3] = {
    {153, 0, 204}, {255, 0, 255}, {0, 0, 255},     {0, 51, 255},    {51, 153, 255}, {0, 153, 153},
    {0, 255, 255}, {102, 255, 204}, {0, 255, 0},   {102, 255, 51},  {204, 255, 153}, {255, 255, 0},
    {255, 204, 102}, {255, 102, 51}, {255, 51, 51}, {255, 0, 0},
};

// RGB565_SWAPPED (上位バイトと下位バイトを入れ替えた並び。ShapoGFX の既定ビルドで常に有効)
inline uint16_t packRgb565(int r, int g, int b) {
  const uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  return (uint16_t)((v >> 8) | (v << 8));
}

inline float smoothstep01(float t) { return t * t * (3 - 2 * t); }

}  // namespace

float sideColumnAngle(int col) { return 2 * PI_F * (1.0f - (col + 0.5f) / SIDE_COLS); }

void pmtPosition(int index, float out[3]) {
  if (index < N_SIDE) {
    const int col = index / SIDE_ROWS, row = index % SIDE_ROWS;
    const float th = sideColumnAngle(col);
    out[0] = RADIUS * sinf(th);
    out[1] = HEIGHT / 2 - (row + 0.5f) * (HEIGHT / SIDE_ROWS);
    out[2] = RADIUS * cosf(th);
    return;
  }
  const bool top = index < N_SIDE + N_CAP;
  const int local = index - (top ? N_SIDE : N_SIDE + N_CAP);
  const int x = local / CAP_COLS, y = local % CAP_COLS;
  const float u = (x + 0.5f) / CAP_COLS, v = (y + 0.5f) / CAP_COLS;
  out[0] = (2 * u - 1) * RADIUS;
  out[1] = top ? HEIGHT / 2 : -HEIGHT / 2;
  out[2] = (top ? 1 : -1) * (1 - 2 * v) * RADIUS;
}

void Detector::cellColor(Cell c, uint8_t rgb[3]) {
  if (c == 0 || c > CELL_PALETTE_COUNT) {
    rgb[0] = UNLIT_RGB[0];
    rgb[1] = UNLIT_RGB[1];
    rgb[2] = UNLIT_RGB[2];
    return;
  }
  const uint8_t *l = LEGEND_RGB[c - 1];
  const int r = l[0], g = l[1], b = l[2];
  if (r > g && b > g && r <= b * 1.05f) {
    // 青〜マゼンタ: 紫寄りほど暗い青にする
    const float purpleness = r < b ? (float)r / b : 1.0f;
    rgb[0] = 0;
    rgb[1] = (uint8_t)g;
    rgb[2] = (uint8_t)(b * (1 - PURPLE_DARKEN * purpleness) + 0.5f);
  } else {
    rgb[0] = (uint8_t)r;
    rgb[1] = (uint8_t)g;
    rgb[2] = (uint8_t)b;
  }
}

void Detector::init(uint16_t *pixels) {
  sidePixels_ = pixels;
  topPixels_ = pixels + SIDE_TEX_W * SIDE_TEX_H;
  bottomPixels_ = topPixels_ + CAP_TEX_W * CAP_TEX_H;
  for (int c = 0; c < SIDE_COLS; c++) {
    const float th = sideColumnAngle(c);
    colX_[c] = RADIUS * sinf(th);
    colZ_[c] = RADIUS * cosf(th);
  }
  for (int r = 0; r < SIDE_ROWS; r++) rowY_[r] = HEIGHT / 2 - (r + 0.5f) * (HEIGHT / SIDE_ROWS);
  for (int i = 0; i < CAP_COLS; i++) {
    const float t = (i + 0.5f) / CAP_COLS;
    capX_[i] = (2 * t - 1) * RADIUS;
    capZ_[i] = (1 - 2 * t) * RADIUS;  // 上面の z。底面は符号を反転する
  }
  memset(pixels, 0, sizeof(uint16_t) * TEXTURE_PIXELS);
  sideTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, SIDE_TEX_W, SIDE_TEX_H, sidePixels_);
  topTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, CAP_TEX_W, CAP_TEX_H, topPixels_);
  bottomTex_ = g2::makeTexture(g2::PixelFormat::RGB565_SWAPPED, CAP_TEX_W, CAP_TEX_H, bottomPixels_);
  cells_ = nullptr;
}

void Detector::setCells(const Cell *cells) { cells_ = cells; }

void Detector::updateTextures(float brightness, const float *center, float revealRadius, float blur) {
  // 色 → 明るさ係数を掛けた RGB565 の表。係数は 1/64 刻み
  uint16_t lut[65][CELL_PALETTE_COUNT + 1];
  for (int k = 0; k <= 64; k++) {
    const float f = brightness * TEXTURE_GAIN * k / 64.0f;
    for (int c = 0; c <= CELL_PALETTE_COUNT; c++) {
      uint8_t rgb[3];
      cellColor((Cell)c, rgb);
      lut[k][c] = packRgb565((int)(rgb[0] * f + 0.5f), (int)(rgb[1] * f + 0.5f), (int)(rgb[2] * f + 0.5f));
    }
  }
  const bool reveal = revealRadius >= 0;
  const float r1 = revealRadius, r0 = revealRadius - blur;
  const float r1sq = r1 * r1, r0sq = r0 > 0 ? r0 * r0 : -1.0f;
  const float cx = center ? center[0] : 0, cy = center ? center[1] : 0, cz = center ? center[2] : 0;
  const float invBlur = blur > 1e-6f ? 1.0f / blur : 0;

  // 前線の係数 (0..64)。距離の 2 乗で大半を判定し、前線の近くだけ平方根を取る
  auto factor = [&](float x, float y, float z) -> int {
    if (!reveal) return 64;
    const float dx = x - cx, dy = y - cy, dz = z - cz;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 >= r1sq) return 0;
    if (d2 <= r0sq) return 64;
    const float d = sqrtf(d2);
    const float t = (d - r0) * invBlur;  // 0..1
    return (int)((1 - smoothstep01(t < 0 ? 0 : (t > 1 ? 1 : t))) * 64 + 0.5f);
  };

  int i = 0;
  for (int c = 0; c < SIDE_COLS; c++) {
    const float x = colX_[c], z = colZ_[c];
    uint16_t *dst = sidePixels_ + c;
    for (int r = 0; r < SIDE_ROWS; r++, i++, dst += SIDE_TEX_W) {
      const Cell cell = cells_ ? cells_[i] : 0;
      *dst = lut[factor(x, rowY_[r], z)][cell];
    }
  }
  for (int face = 0; face < 2; face++) {
    const bool top = face == 0;
    const float y = top ? HEIGHT / 2 : -HEIGHT / 2;
    uint16_t *pixels = top ? topPixels_ : bottomPixels_;
    for (int x = 0; x < CAP_COLS; x++) {
      const float px = capX_[x];
      uint16_t *dst = pixels + x;
      for (int yy = 0; yy < CAP_COLS; yy++, i++, dst += CAP_TEX_W) {
        const float pz = top ? capZ_[yy] : -capZ_[yy];
        const Cell cell = cells_ ? cells_[i] : 0;
        // 円の外は描かない (黒のまま)
        const float dx = px / RADIUS, dz = pz / RADIUS;
        *dst = (dx * dx + dz * dz > 1.0f) ? 0 : lut[factor(px, y, pz)][cell];
      }
    }
  }
}

}  // namespace tkapp
