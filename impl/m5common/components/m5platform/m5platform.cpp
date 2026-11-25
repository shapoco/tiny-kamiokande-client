#include "m5platform.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>

#include "m5panel.hpp"
#include "tk_runner.hpp"

namespace m5platform {

namespace {

class M5Board : public tkrunner::Board {
 public:
  explicit M5Board(const Config &cfg) : cfg_(cfg) {}

  bool init(const tkrunner::Config &rc, int &w, int &h) override {
    auto m5cfg = M5.config();
    m5cfg.internal_spk = false;
    m5cfg.internal_mic = false;
    m5cfg.internal_imu = false;
    m5cfg.internal_rtc = false;
    M5.begin(m5cfg);
    M5.Display.setRotation(cfg_.rotation);
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(TFT_BLACK);
    w = M5.Display.width() / cfg_.scale;
    h = M5.Display.height() / cfg_.scale;
    const int stripH = std::min(rc.stripH, h);
    return panelOut().init(M5.Display, w, h, cfg_.scale, stripH);
  }

  tkrunner::PanelOut &panel() override { return panelOut(); }

  // M5Unified の指の id ごとにアプリのポインタ id を割り当てる
  void poll(tkapp::App &app) override {
    M5.update();
    bool down[tkapp::MAX_POINTERS] = {};
    int xs[tkapp::MAX_POINTERS] = {}, ys[tkapp::MAX_POINTERS] = {};
    const int n = std::min((int)M5.Touch.getCount(), 4);
    for (int i = 0; i < n; i++) {
      const auto &d = M5.Touch.getDetail(i);
      if (!d.isPressed()) continue;
      int slot = -1;
      for (int k = 0; k < tkapp::MAX_POINTERS; k++) {
        if (wasDown_[k] && ids_[k] == d.id) slot = k;
      }
      if (slot < 0) {
        for (int k = 0; k < tkapp::MAX_POINTERS; k++) {
          if (!wasDown_[k] && !down[k]) {
            slot = k;
            ids_[k] = d.id;
            break;
          }
        }
      }
      if (slot < 0) continue;
      down[slot] = true;
      xs[slot] = d.x / cfg_.scale;
      ys[slot] = d.y / cfg_.scale;
    }
    for (int k = 0; k < tkapp::MAX_POINTERS; k++) {
      if (down[k] && !wasDown_[k]) app.pointerDown(k, xs[k], ys[k]);
      else if (down[k]) app.pointerMove(k, xs[k], ys[k]);
      else if (wasDown_[k]) app.pointerUp(k);
      wasDown_[k] = down[k];
    }
  }

 private:
  const Config &cfg_;
  bool wasDown_[tkapp::MAX_POINTERS] = {};
  int ids_[tkapp::MAX_POINTERS] = {-1, -1};
};

}  // namespace

void run(const Config &cfg) {
  static M5Board board(cfg);
  tkrunner::Config rc = {};
  rc.name = cfg.name;
  rc.stripH = cfg.stripH;
  rc.multiTouch = true;
  rc.demo = cfg.demo;
  rc.demoIntervalSec = cfg.demoIntervalSec;
  tkrunner::run(board, rc);
}

}  // namespace m5platform
