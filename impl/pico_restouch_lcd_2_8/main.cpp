// Waveshare Pico-ResTouch-LCD-2.8 + Raspberry Pi Pico 2 W 版。
// 画面とレイアウトは CoreS3 版と同じ 240x320 の縦長。入力は抵抗膜タッチ (1 点) で、
// ズームはピンチの代わりにダブルタップ (拡大) と長押し (縮小)。
// タッチはパネルと SPI を共用するので、バスが空く onPanelIdle() で読んで poll() でアプリに渡す

#include <algorithm>
#include <cstdio>

#include "runner.hpp"
#include "touch_xpt2046.hpp"

#ifndef TKAPP_DEMO
#define TKAPP_DEMO 0
#endif

namespace {

// 帯バッファ 2 本で 30 KB (CoreS3 と同じ)。内蔵 RAM しか無いので小さめ
constexpr int STRIP_H = 32;

class ResTouchBoard : public tkrp2::Board {
 public:
  void initDevices() override {
    rp2common::Xpt2046Config tc = {};
    tc.spi = SPI_INSTANCE(RP2COMMON_LCD_SPI);
    tc.misoPin = RP2COMMON_TOUCH_MISO_PIN;
    tc.csPin = RP2COMMON_TOUCH_CS_PIN;
    tc.irqPin = RP2COMMON_TOUCH_IRQ_PIN;
    tc.busBaud = tkrp2::panel().baud();
    touch_.init(tc, tkrp2::panel().width(), tkrp2::panel().height());
  }

  void log() override { std::printf("touch: XPT2046 on the panel SPI\n"); }

  void onPanelIdle() override { down_ = touch_.read(x_, y_); }

  // タッチはパネル本来の縦向きの座標で、画面の座標と一致する
  void poll(tkapp::App &app) override {
    const int w = tkrp2::panel().width(), h = tkrp2::panel().height();
    int x = std::clamp(x_, 0, w - 1);
    int y = std::clamp(y_, 0, h - 1);
    if (RP2COMMON_LCD_FLIP) {
      x = w - 1 - x;
      y = h - 1 - y;
    }
    if (down_ && !wasDown_) {
      app.pointerDown(0, x, y);
    } else if (down_) {
      app.pointerMove(0, x, y);
    } else if (wasDown_) {
      app.pointerUp(0);
    }
    wasDown_ = down_;
  }

 private:
  rp2common::Xpt2046Touch touch_;
  bool down_ = false;  // onPanelIdle() で読んだ最新の状態
  int x_ = 0, y_ = 0;
  bool wasDown_ = false;
};

}  // namespace

int main() {
  tkrp2::Config cfg = {};
  cfg.name = "Waveshare Pico-ResTouch-LCD-2.8 + Pico 2 W";
  cfg.stripH = STRIP_H;
  cfg.multiTouch = false;
  cfg.demo = TKAPP_DEMO != 0;
  cfg.demoIntervalSec = 15.0f;
  static ResTouchBoard board;
  tkrp2::run(board, cfg);
}
