// M5Stack CoreS3 版

#include "m5platform.hpp"

#ifndef TKAPP_DEMO
#define TKAPP_DEMO 0
#endif

extern "C" void app_main(void) {
  m5platform::Config cfg = {};
  cfg.name = "M5Stack CoreS3";
  cfg.rotation = 2;  // USB コネクタが下
  cfg.scale = 1;
  // 帯バッファ 2 本で 30 KB。内蔵 RAM を WiFi に残すため小さめ
  cfg.stripH = 32;
  cfg.demo = TKAPP_DEMO != 0;
  cfg.demoIntervalSec = 15.0f;
  m5platform::run(cfg);
}
