#include "panel_spi.hpp"

#include <cstdlib>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

namespace rp2common {

namespace {

// 初期化コマンド列。要素は コマンド、データ長、データ...。0xFF で終わる。
// 画素形式はここで設定する (16 bit)。

// RP2350-Touch-LCD-2 の ST7789T3。Waveshare のデモ (LCD_2in.c) に倣い、
// コマンドセットのアンロック (F0 C3、F0 96) とロック (F0 3C、F0 69) の間に
// ベンダの電源とガンマの設定を置く
const uint8_t INIT_ST7789T3[] = {
    0x3A, 1, 0x05,  // COLMOD
    0xF0, 1, 0xC3,
    0xF0, 1, 0x96,
    0xB4, 1, 0x01,
    0xB7, 1, 0xC6,
    0xC0, 2, 0x80, 0x45,
    0xC1, 1, 0x13,
    0xC2, 1, 0xA7,
    0xC5, 1, 0x0A,
    0xE8, 8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33,
    0xE0, 14, 0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30, 0x33, 0x47, 0x17, 0x13, 0x13, 0x2B, 0x31,
    0xE1, 14, 0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F, 0x33, 0x47, 0x38, 0x15, 0x16, 0x2C, 0x32,
    0xF0, 1, 0x3C,
    0xF0, 1, 0x69,
    0xFF,
};

// PicoCalc の ST7365P。ClockworkPi の設定 (ShapoNES の PicoCalc 移植と同じ)。
// COLMOD の 0x65 で 16 bit 色になる
const uint8_t INIT_ST7365P[] = {
    0xF0, 1, 0xC3,
    0xF0, 1, 0x96,
    0x3A, 1, 0x65,  // COLMOD
    0xB1, 1, 0xA0,  // フレームレート
    0xB4, 1, 0x00,
    0xB7, 1, 0xC6,
    0xB9, 2, 0x02, 0xE0,
    0xC0, 2, 0x80, 0x06,
    0xC1, 1, 0x15,
    0xC2, 1, 0xA7,
    0xC5, 1, 0x04,
    0xE8, 8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xAA, 0x33,
    0xE0, 14, 0xF0, 0x06, 0x0F, 0x05, 0x04, 0x20, 0x37, 0x33, 0x4C, 0x37, 0x13, 0x14, 0x2B, 0x31,
    0xE1, 14, 0xF0, 0x11, 0x1B, 0x11, 0x0F, 0x0A, 0x37, 0x43, 0x4C, 0x37, 0x13, 0x13, 0x2C, 0x32,
    0xF0, 1, 0x3C,
    0xF0, 1, 0x69,
    0x35, 1, 0x00,  // TEON
    0xFF,
};

// Xiamocon の ST7789。Xiamocon SDK は画素形式しか設定しない
const uint8_t INIT_ST7789[] = {
    0x3A, 1, 0x55,  // COLMOD
    0xFF,
};

// Pico-ResTouch-LCD-2.8 の ST7789VW。Waveshare のデモ (LCD_Driver.c の 2.8 インチの分) に倣う
const uint8_t INIT_ST7789VW[] = {
    0x3A, 1, 0x55,  // COLMOD
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x35,
    0xBB, 1, 0x28,
    0xC0, 1, 0x3C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x0B,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xD0, 0x01, 0x08, 0x0F, 0x11, 0x2A, 0x36, 0x55, 0x44, 0x3A, 0x0B, 0x06, 0x11, 0x20,
    0xE1, 14, 0xD0, 0x02, 0x07, 0x0A, 0x0B, 0x18, 0x34, 0x43, 0x4A, 0x2B, 0x1B, 0x1C, 0x22, 0x1F,
    0x55, 1, 0xB0,  // WRCACE
    0xFF,
};

// PicoSystem の ST7789。PicoSystem SDK (hardware.cpp) と同じ値。COLMOD は SDK の 12 bit (0x03)
// ではなく 16 bit (0x55)。順序も SDK と同じにする (init() を参照)
const uint8_t INIT_ST7789_PICOSYSTEM[] = {
    0x35, 1, 0x00,  // TEON
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0x3A, 1, 0x55,  // COLMOD
    0x26, 1, 0x01,  // GAMSET
    0xB7, 1, 0x14,
    0xBB, 1, 0x25,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x12,
    0xC4, 1, 0x20,
    0xD0, 2, 0xA4, 0xA1,
    0xC6, 1, 0x1E,
    0xE0, 14, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
    0xE1, 14, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
    0xFF,
};

constexpr uint8_t MADCTL_MY = 0x80, MADCTL_MX = 0x40;

// CS と DC を切り替えてからクロックを出すまで、最後のクロックから CS を上げるまで、
// CS を上げてから次に下げるまでの間隔。パネルのセットアップ・ホールド時間
// (ST7789 の tCSS / tCSH / tCHW は 15〜40 ns) を 300 MHz でも満たす。PicoCalc の
// ST7365P は間隔が無いと帯の途中からビットがずれて色が化けた (実機で確認)
constexpr int PIN_SETTLE_NS = 100;

const uint8_t *initSequence(PanelKind kind) {
  switch (kind) {
    case PanelKind::ST7789T3: return INIT_ST7789T3;
    case PanelKind::ST7365P: return INIT_ST7365P;
    case PanelKind::ST7789: return INIT_ST7789;
    case PanelKind::ST7789VW: return INIT_ST7789VW;
    case PanelKind::ST7789_PICOSYSTEM: return INIT_ST7789_PICOSYSTEM;
  }
  return INIT_ST7789;
}

// 反転表示 (INVON) にするパネル。ST7789VW は Waveshare のデモが送らない (送ると色が反転する)
bool invertDisplay(PanelKind kind) { return kind != PanelKind::ST7789VW; }

}  // namespace

bool SpiPanel::init(const PanelConfig &cfg, int stripH) {
  cfg_ = cfg;
  settleCycles_ = (uint32_t)((uint64_t)clock_get_hz(clk_sys) * PIN_SETTLE_NS / 1000000000u) + 1;

  // 制御ピン。CS、DC は High で待機
  const int outs[] = {cfg.csPin, cfg.dcPin, cfg.rstPin};
  for (int pin : outs) {
    if (pin < 0) continue;
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, GPIO_OUT);
    gpio_put((uint)pin, 1);
  }

  // バックライトは表示が出るまで消しておく
  if (cfg.blPin >= 0) {
    gpio_set_function((uint)cfg.blPin, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num((uint)cfg.blPin);
    pwm_set_wrap(slice, 255);
    pwm_set_clkdiv(slice, (float)clock_get_hz(clk_sys) / (256 * 20000.0f));  // 20 kHz
    pwm_set_gpio_level((uint)cfg.blPin, 0);
    pwm_set_enabled(slice, true);
  }

  // SPI モード 0、8 bit。高速なので立ち上がりを速く
  baud_ = spi_init(cfg.spi, cfg.baud);
  cmdBaud_ = cfg.cmdBaud ? spi_set_baudrate(cfg.spi, cfg.cmdBaud) : baud_;
  spi_set_baudrate(cfg.spi, baud_);
  spi_set_format(cfg.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  const int spiPins[] = {cfg.sckPin, cfg.mosiPin};
  for (int pin : spiPins) {
    gpio_set_function((uint)pin, GPIO_FUNC_SPI);
    gpio_set_slew_rate((uint)pin, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength((uint)pin, GPIO_DRIVE_STRENGTH_8MA);
  }
  gpio_set_slew_rate((uint)cfg.dcPin, GPIO_SLEW_RATE_FAST);

  // ハードウェアリセット (ピンを共用するタッチコントローラも一緒にリセットされる)
  if (cfg.rstPin >= 0) {
    gpio_put((uint)cfg.rstPin, 1);
    sleep_ms(10);
    gpio_put((uint)cfg.rstPin, 0);
    sleep_ms(20);
    gpio_put((uint)cfg.rstPin, 1);
    sleep_ms(120);
  }

  uint8_t madctl = cfg.madctl;
  if (cfg.flip) madctl ^= MADCTL_MY | MADCTL_MX;
  if (cfg.kind == PanelKind::ST7789_PICOSYSTEM) {
    // PicoSystem SDK の順序: リセット、設定、反転、スリープ解除、表示 (他の順序では
    // 黒いままのパネルがあった)
    command(0x01);  // SWRESET
    sleep_ms(5);
    command(0x36, &madctl, 1);
    for (const uint8_t *p = initSequence(cfg.kind); *p != 0xFF; p += 2 + p[1]) {
      command(p[0], p + 2, p[1]);
    }
    command(0x21);  // INVON
    sleep_ms(115);
    command(0x11);  // SLPOUT
    clear();
    command(0x29);  // DISPON
  } else {
    if (cfg.kind == PanelKind::ST7789) {
      command(0x01);  // SWRESET
      sleep_ms(150);
    }
    command(0x11);  // SLPOUT
    sleep_ms(120);
    command(0x36, &madctl, 1);
    for (const uint8_t *p = initSequence(cfg.kind); *p != 0xFF; p += 2 + p[1]) {
      command(p[0], p + 2, p[1]);
    }
    sleep_ms(120);
    if (invertDisplay(cfg.kind)) command(0x21);  // INVON
    clear();
    command(0x29);  // DISPON
    sleep_ms(20);
  }

  // 帯バッファ
  const size_t bytes = (size_t)cfg.frameW * stripH * 2;
  for (int i = 0; i < 2; i++) {
    buf_[i] = (uint16_t *)std::malloc(bytes);  // 8 バイト境界
    if (!buf_[i]) return false;
    std::memset(buf_[i], 0, bytes);
  }

  // DMA: メモリから SPI のデータレジスタへ、TX の DREQ に合わせてバイト転送
  dma_ = dma_claim_unused_channel(true);
  return true;
}

void SpiPanel::settle() { busy_wait_at_least_cycles(settleCycles_); }

void SpiPanel::command(uint8_t cmd, const uint8_t *data, int n) {
  drain();
  if (cmdBaud_ != baud_) spi_set_baudrate(cfg_.spi, cmdBaud_);
  gpio_put((uint)cfg_.dcPin, 0);
  gpio_put((uint)cfg_.csPin, 0);
  settle();
  spi_write_blocking(cfg_.spi, &cmd, 1);
  settle();
  gpio_put((uint)cfg_.dcPin, 1);
  settle();
  if (n > 0) {
    spi_write_blocking(cfg_.spi, data, (size_t)n);
    settle();
  }
  gpio_put((uint)cfg_.csPin, 1);
  settle();
  if (cmdBaud_ != baud_) spi_set_baudrate(cfg_.spi, baud_);
}

void SpiPanel::beginPixels(int x, int y, int w, int h) {
  const int x1 = x + w - 1, y1 = y + h - 1;
  const uint8_t caset[] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t raset[] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1};
  command(0x2A, caset, 4);
  command(0x2B, raset, 4);
  command(0x2C);  // RAMWR
  gpio_put((uint)cfg_.csPin, 0);  // 画素は DC High、CS Low で続ける
  settle();
}

void SpiPanel::submit(int i, int y, int rows) {
  beginPixels((cfg_.nativeW - cfg_.frameW) / 2, (cfg_.nativeH - cfg_.frameH) / 2 + y,
              cfg_.frameW, rows);
  dma_channel_config c = dma_channel_get_default_config((uint)dma_);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, spi_get_dreq(cfg_.spi, true));
  dma_channel_configure((uint)dma_, &c, &spi_get_hw(cfg_.spi)->dr, buf_[i],
                        (uint32_t)cfg_.frameW * rows * 2, true);
  pending_ = true;
}

void SpiPanel::drain() {
  if (!pending_) return;
  dma_channel_wait_for_finish_blocking((uint)dma_);
  // DMA が終わってから最後のバイトが FIFO を出る
  while (spi_is_busy(cfg_.spi)) tight_loop_contents();
  // 受信側は誰も読まないので、空にしてオーバーランを消す
  while (spi_is_readable(cfg_.spi)) (void)spi_get_hw(cfg_.spi)->dr;
  spi_get_hw(cfg_.spi)->icr = SPI_SSPICR_RORIC_BITS;
  settle();
  gpio_put((uint)cfg_.csPin, 1);
  settle();
  pending_ = false;
}

void SpiPanel::clear() {
  beginPixels(0, 0, cfg_.nativeW, cfg_.nativeH);
  const uint8_t zero[64] = {};
  for (int n = cfg_.nativeW * cfg_.nativeH * 2; n > 0; n -= (int)sizeof(zero)) {
    spi_write_blocking(cfg_.spi, zero, (size_t)(n < (int)sizeof(zero) ? n : (int)sizeof(zero)));
  }
  settle();
  gpio_put((uint)cfg_.csPin, 1);
  settle();
}

void SpiPanel::setBacklight(int level) {
  if (cfg_.blPin < 0) return;
  // 256 で周期の間ずっと High
  const int v = level <= 0 ? 0 : level >= 255 ? 256 : level;
  pwm_set_gpio_level((uint)cfg_.blPin, (uint16_t)v);
}

void SpiPanel::displayOff() {
  setBacklight(0);
  command(0x28);  // DISPOFF
  command(0x10);  // SLPIN
  sleep_ms(120);
}

}  // namespace rp2common
