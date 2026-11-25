// 帯 → PPA → MIPI-DSI のフレームバッファ (M5Stack Tab5)。ShapoGFX の
// example/m5common/panel_ppa.cpp と Devour Sphere の Tab5 版に倣う。
//
// PPA が帯ごとに拡大とバイト入れ替えをしてフレームバッファに書く。rotation 2
// では 180° 回して下から詰める。
//
// フレームバッファを得るため M5GFX の Panel_DSI (M5GFX.h から公開されていない)
// に触れるので、M5GFX のバージョンを固定している。

#include <M5GFX.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstring>

#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"
#include "m5panel.hpp"

namespace m5platform {

namespace {

void flushForDma(const void *p, size_t bytes) {
  static bool complained = false;
  const esp_err_t err = esp_cache_msync((void *)p, bytes,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  if (err != ESP_OK && !complained) {
    complained = true;
    std::printf("cache msync refused the strip buffer, err %d\n", (int)err);
  }
}

bool IRAM_ATTR onPpaDone(ppa_client_handle_t, ppa_event_data_t *, void *user) {
  auto sem = (SemaphoreHandle_t)user;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &woken);
  return woken == pdTRUE;
}

class PpaPanelOut : public M5PanelOut {
 public:
  bool init(m5gfx::M5GFX &gfx, int w, int h, int scale, int stripH) override {
    auto *panel = static_cast<lgfx::Panel_DSI *>(gfx.getPanel());
    if (!panel || !panel->config_detail().buffer) {
      std::printf("no DSI panel framebuffer\n");
      return false;
    }
    panelW_ = panel->config().panel_width;
    panelH_ = panel->config().panel_height;
    rotation_ = gfx.getRotation() & 3;
    if ((rotation_ != 0 && rotation_ != 2) || w * scale != panelW_ || h * scale != panelH_) {
      std::printf("unexpected geometry: panel %dx%d, rotation %d, frame %dx%d\n", panelW_, panelH_,
                  rotation_, w, h);
      return false;
    }
    panelFb_ = (uint16_t *)panel->config_detail().buffer;
    panelBytes_ = (uint32_t)panelW_ * panelH_ * 2;
    w_ = w;
    scale_ = scale;

    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = 2;
    ppa_client_handle_t ppa = nullptr;
    if (ppa_register_client(&cfg, &ppa) != ESP_OK) {
      std::printf("ppa_register_client failed\n");
      return false;
    }
    ppa_ = ppa;
    done_ = xSemaphoreCreateBinary();
    if (!done_) return false;
    ppa_event_callbacks_t cbs = {};
    cbs.on_trans_done = onPpaDone;
    if (ppa_client_register_event_callbacks(ppa, &cbs) != ESP_OK) {
      std::printf("ppa callbacks failed\n");
      return false;
    }

    // msync のため L2 キャッシュラインに揃える
    const size_t bytes = (size_t)w * stripH * 2;
    for (int i = 0; i < 2; i++) {
      buf_[i] = (uint16_t *)heap_caps_aligned_alloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE, bytes,
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (!buf_[i]) {
        std::printf("strip buffer (%u bytes) did not fit, largest block %u\n", (unsigned)bytes,
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        return false;
      }
      memset(buf_[i], 0, bytes);
    }
    esp_cache_msync(panelFb_, panelBytes_, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    return true;
  }

  void submit(int i, int y, int rows) override {
    const uint32_t v0 = (uint32_t)y * scale_;
    const uint32_t strip = (uint32_t)rows * scale_;

    ppa_srm_oper_config_t op = {};
    op.in.buffer = buf_[i];
    op.in.pic_w = w_;
    op.in.pic_h = rows;
    op.in.block_w = w_;
    op.in.block_h = rows;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.out.buffer = panelFb_;
    op.out.buffer_size = panelBytes_;
    op.out.pic_w = panelW_;
    op.out.pic_h = panelH_;
    op.out.block_offset_x = 0;
    op.out.block_offset_y = rotation_ == 0 ? v0 : (panelH_ - v0 - strip);
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.rotation_angle = rotation_ == 0 ? PPA_SRM_ROTATION_ANGLE_0 : PPA_SRM_ROTATION_ANGLE_180;
    op.scale_x = (float)scale_;
    op.scale_y = (float)scale_;
    op.byte_swap = true;
    op.mode = PPA_TRANS_MODE_NON_BLOCKING;
    op.user_data = done_;

    flushForDma(buf_[i], (size_t)w_ * rows * 2);
    const esp_err_t err = ppa_do_scale_rotate_mirror((ppa_client_handle_t)ppa_, &op);
    if (err != ESP_OK) {
      static bool complained = false;
      if (!complained) {
        complained = true;
        std::printf("ppa refused the strip, err %d, y %u\n", (int)err, (unsigned)op.out.block_offset_y);
      }
      return;
    }
    pending_ = true;
  }

  void drain() override {
    if (!pending_) return;
    xSemaphoreTake((SemaphoreHandle_t)done_, portMAX_DELAY);
    pending_ = false;
  }

 private:
  void *ppa_ = nullptr;
  void *done_ = nullptr;
  uint16_t *panelFb_ = nullptr;
  uint32_t panelBytes_ = 0;
  int panelW_ = 0, panelH_ = 0, rotation_ = 0;
  int w_ = 0, scale_ = 1;
  bool pending_ = false;
};

}  // namespace

M5PanelOut &panelOut() {
  static PpaPanelOut out;
  return out;
}

}  // namespace m5platform
