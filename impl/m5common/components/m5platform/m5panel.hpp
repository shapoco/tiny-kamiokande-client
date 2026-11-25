#ifndef M5PLATFORM_M5PANEL_HPP
#define M5PLATFORM_M5PANEL_HPP

// M5Stack のパネルへの帯の転送。実装は panel_spi.cpp (ESP32-S3) と panel_ppa.cpp (ESP32-P4)

#include "tk_runner.hpp"

namespace m5gfx {
class M5GFX;
}

namespace m5platform {

class M5PanelOut : public tkrunner::PanelOut {
 public:
  // M5.begin() と回転の設定の後に 1 回呼ぶ。scale はフレームの 1 ピクセルあたりのパネルのピクセル数
  virtual bool init(m5gfx::M5GFX &gfx, int w, int h, int scale, int stripH) = 0;
};

M5PanelOut &panelOut();

}  // namespace m5platform

#endif
