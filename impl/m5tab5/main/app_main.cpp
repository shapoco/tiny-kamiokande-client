// M5Stack Tab5 版。パネルの半分の解像度 (360x640) で描き、PPA で 2 倍に拡大する

#include "m5platform.hpp"

#ifdef TKAPP_CP_OTA
#include "cp_ota.hpp"
#endif

#ifndef TKAPP_DEMO
#define TKAPP_DEMO 0
#endif

extern "C" void app_main(void) {
#ifdef TKAPP_CP_OTA
  cpOtaRegister();
#endif
  m5platform::Config cfg = {};
  cfg.name = "M5Stack Tab5";
  cfg.rotation = 0;
  cfg.scale = 2;
  cfg.stripH = 40;  // 帯バッファ 2 本で 58 KB。内蔵 RAM を esp_hosted の受信バッファに残す
  cfg.demo = TKAPP_DEMO != 0;
  cfg.demoIntervalSec = 15.0f;
  m5platform::run(cfg);
}
