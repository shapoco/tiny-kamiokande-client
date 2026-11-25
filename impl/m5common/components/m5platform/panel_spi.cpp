// 帯 → SPI DMA → パネル (M5Stack CoreS3)。ShapoGFX の example/m5common/ に倣う。
// 回転はパネルコントローラ (MADCTL) が行い、RGB565_SWAPPED はパネルの
// バイト順そのものなので、帯をそのまま送れる。トランザクションは開いたまま。

#include <M5GFX.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

#include "m5panel.hpp"

namespace m5platform {

namespace {

class SpiPanelOut : public M5PanelOut {
 public:
  bool init(m5gfx::M5GFX &gfx, int w, int h, int scale, int stripH) override {
    (void)h;
    if (scale != 1) {
      std::printf("SPI panel: scale %d is not supported\n", scale);
      return false;
    }
    gfx_ = &gfx;
    w_ = w;
    const size_t bytes = (size_t)w * stripH * 2;
    for (int i = 0; i < 2; i++) {
      buf_[i] = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
      if (!buf_[i]) {
        std::printf("strip buffer (%u bytes) did not fit, largest block %u\n", (unsigned)bytes,
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        return false;
      }
      memset(buf_[i], 0, bytes);
    }
    gfx_->startWrite();
    return true;
  }

  void submit(int i, int y, int rows) override {
    gfx_->setWindow(0, y, w_ - 1, y + rows - 1);
    gfx_->writePixelsDMA(buf_[i], (int32_t)w_ * rows, false);
    pending_ = true;
  }

  void drain() override {
    if (!pending_) return;
    gfx_->waitDMA();
    pending_ = false;
  }

 private:
  m5gfx::M5GFX *gfx_ = nullptr;
  int w_ = 0;
  bool pending_ = false;
};

}  // namespace

M5PanelOut &panelOut() {
  static SpiPanelOut out;
  return out;
}

}  // namespace m5platform
