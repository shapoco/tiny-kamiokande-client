#ifndef RP2COMMON_TOUCH_XPT2046_HPP
#define RP2COMMON_TOUCH_XPT2046_HPP

// XPT2046 (抵抗膜式、1 点) をパネルと共用の SPI で読む (Pico-ResTouch-LCD-2.8)。
// 押下は PENIRQ と圧力で判定し、数回の中央値を固定の写像 (touch_xpt2046.cpp の RAW_* など)
// で画面のピクセルに直す。
//
// SPI はパネルのもので、バスが空いているとき (Platform::onPanelIdle()) にだけ読む。
// 読む間はクロックを XPT2046 の上限まで落とし、終わったらパネルの値に戻す。

#include <cstdint>

#include "hardware/spi.h"

namespace rp2common {

struct Xpt2046Config {
  spi_inst_t *spi;   // パネルと同じ。SCK / MOSI はパネルが設定済み
  int misoPin;
  int csPin;
  int irqPin;        // PENIRQ
  uint32_t busBaud;  // 読み終わったら戻す SPI クロック (パネルのもの)
};

class Xpt2046Touch {
 public:
  void init(const Xpt2046Config &cfg, int screenW, int screenH);

  // 押されていれば true で x、y に画面の座標 (画面内に収めた値) を入れる。
  // PENIRQ が High なら SPI には触らない
  bool read(int &x, int &y);

 private:
  Xpt2046Config cfg_ = {};
  int w_ = 0, h_ = 0;

  uint16_t convert(uint8_t cmd);
  bool pressed();
};

}  // namespace rp2common

#endif
