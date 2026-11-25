#ifndef TKAPP_DETECTOR_HPP
#define TKAPP_DETECTOR_HPP

// 検出器の形 (PMT の座標) と 3 面のテクスチャ。
// テクスチャは 1 PMT = 1 テクセルの RGB565 で、点灯の演出 (中心から広がる前線) と全体の明るさを
// 掛けた色を毎回書き直す。SPEC.md 3.1、4.2 の対応に従う

#include <cstdint>

#include "shapoco/gfx2d/surface.hpp"
#include "tkapp/event.hpp"

namespace tkapp {

// 3 面のテクスチャの画素数 (RGB565、側面 + 上面 + 底面)
constexpr int TEXTURE_PIXELS = SIDE_TEX_W * SIDE_TEX_H + 2 * CAP_TEX_W * CAP_TEX_H;

// PMT の通し番号 → モデル座標 (単位: ピッチ。側面 index = col * SIDE_ROWS + row、上下面 index = x * CAP_COLS + y)
void pmtPosition(int index, float out[3]);

// 側面の列 → 方位 θ (ラジアン)。位置は (R sin θ, y, R cos θ)
float sideColumnAngle(int col);

class Detector {
 public:
  // pixels: TEXTURE_PIXELS 個の画素の置き場所 (毎フレーム読むので内蔵 RAM に置く)
  void init(uint16_t *pixels);

  // 表示するイベントのセル (N_PMTS 個)。nullptr なら全て未点灯
  void setCells(const Cell *cells);

  // テクスチャを書き直す。brightness は全体の明るさ 0..1。
  // revealRadius < 0 なら全部点いている。そうでなければ center からの直線距離が
  // revealRadius 未満の PMT が点き、手前 blur の幅でなめらかに暗くなる
  void updateTextures(float brightness, const float *revealCenter, float revealRadius, float blur);

  const shapoco::gfx2d::Texture &sideTexture() const { return sideTex_; }
  const shapoco::gfx2d::Texture &topTexture() const { return topTex_; }
  const shapoco::gfx2d::Texture &bottomTexture() const { return bottomTex_; }

  // セルの値 → 表示色 (0..255)。index.html の displayRgb (紫寄りを暗くする) と同じ
  static void cellColor(Cell c, uint8_t rgb[3]);

 private:
  const Cell *cells_ = nullptr;
  uint16_t *sidePixels_ = nullptr, *topPixels_ = nullptr, *bottomPixels_ = nullptr;
  shapoco::gfx2d::Texture sideTex_, topTex_, bottomTex_;
  // 側面の列の座標 (x, z) と行の y。毎フレームの距離計算に使う
  float colX_[SIDE_COLS], colZ_[SIDE_COLS], rowY_[SIDE_ROWS], capX_[CAP_COLS], capZ_[CAP_COLS];
};

}  // namespace tkapp

#endif
