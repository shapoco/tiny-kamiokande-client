#include "touch_xpt2046.hpp"

#include <algorithm>
#include <cstdio>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#ifndef RP2COMMON_TOUCH_LOG
#define RP2COMMON_TOUCH_LOG 0
#endif

namespace rp2common {
namespace {

constexpr uint32_t TOUCH_HZ = 2 * 1000 * 1000;  // XPT2046 の上限は 2.5 MHz

// 制御バイト: S=1、A2..A0、MODE=0 (12 bit)、SER/DFR=0 (差動)、PD=00 (変換後は省電力、PENIRQ 有効)
constexpr uint8_t CMD_X = 0xD0;
constexpr uint8_t CMD_Y = 0x90;
constexpr uint8_t CMD_Z1 = 0xB0;
constexpr uint8_t CMD_Z2 = 0xC0;

constexpr int SAMPLES = 3;         // 中央値を取る回数
constexpr int PRESSURE_MIN = 300;  // z1 + (4095 - z2) がこれ未満なら押していない

// 生の 12 bit 値から画面のピクセルへの固定の写像。Waveshare のデモの縦向き (L2R_U2D) の
// 校正値 (x = 0.066626 rx - 20、y = 0.089779 ry - 34) を生値の範囲に直したもの。
// 生値は両軸とも画面と逆向き (デモの座標系はパネルの向きと 180 度違う。実機で確認)。
// 個体で合わなければここを直す
constexpr bool SWAP_XY = false;  // 生の Y が画面の横なら true
constexpr bool FLIP_X = true;
constexpr bool FLIP_Y = true;
constexpr int RAW_X_MIN = 300, RAW_X_MAX = 3902;
constexpr int RAW_Y_MIN = 379, RAW_Y_MAX = 3943;

constexpr uint32_t CS_SETTLE_US = 1;

int median3(int a, int b, int c) { return std::max(std::min(a, b), std::min(std::max(a, b), c)); }

int mapAxis(int raw, int rawMin, int rawMax, int size, bool flip) {
  int v = (int)((int64_t)(raw - rawMin) * size / (rawMax - rawMin));
  if (flip) v = size - 1 - v;
  return std::clamp(v, 0, size - 1);
}

}  // namespace

void Xpt2046Touch::init(const Xpt2046Config &cfg, int screenW, int screenH) {
  cfg_ = cfg;
  w_ = screenW;
  h_ = screenH;
  gpio_set_function((uint)cfg.misoPin, GPIO_FUNC_SPI);
  gpio_init((uint)cfg.csPin);
  gpio_set_dir((uint)cfg.csPin, GPIO_OUT);
  gpio_put((uint)cfg.csPin, 1);
  gpio_init((uint)cfg.irqPin);
  gpio_set_dir((uint)cfg.irqPin, GPIO_IN);
  gpio_pull_up((uint)cfg.irqPin);
  // 省電力に入れて PENIRQ を有効にする
  spi_set_baudrate(cfg_.spi, TOUCH_HZ);
  convert(CMD_X);
  spi_set_baudrate(cfg_.spi, cfg_.busBaud);
}

// 制御バイトを送り、続く 2 バイトから 12 bit の変換結果を取り出す
uint16_t Xpt2046Touch::convert(uint8_t cmd) {
  const uint8_t tx[3] = {cmd, 0, 0};
  uint8_t rx[3] = {};
  gpio_put((uint)cfg_.csPin, 0);
  busy_wait_us(CS_SETTLE_US);
  spi_write_read_blocking(cfg_.spi, tx, rx, sizeof tx);
  busy_wait_us(CS_SETTLE_US);
  gpio_put((uint)cfg_.csPin, 1);
  return (uint16_t)((((rx[1] << 8) | rx[2]) >> 3) & 0x0FFF);
}

bool Xpt2046Touch::pressed() {
  if (gpio_get((uint)cfg_.irqPin)) return false;
  const int z1 = convert(CMD_Z1);
  const int z2 = convert(CMD_Z2);
  return z1 + (4095 - z2) >= PRESSURE_MIN;
}

bool Xpt2046Touch::read(int &x, int &y) {
  if (gpio_get((uint)cfg_.irqPin)) return false;
  spi_set_baudrate(cfg_.spi, TOUCH_HZ);
  bool down = pressed();
  int rawX = 0, rawY = 0;
  if (down) {
    int xs[SAMPLES], ys[SAMPLES];
    convert(CMD_X);  // 最初の変換は不安定なので捨てる
    for (int i = 0; i < SAMPLES; i++) {
      xs[i] = convert(CMD_X);
      ys[i] = convert(CMD_Y);
    }
    rawX = median3(xs[0], xs[1], xs[2]);
    rawY = median3(ys[0], ys[1], ys[2]);
    // 読んでいる間は PENIRQ が止まるので、変換後の値で押下をもう一度確かめる
    down = pressed();
  }
  spi_set_baudrate(cfg_.spi, cfg_.busBaud);
  if (!down) return false;
  const int rx = SWAP_XY ? rawY : rawX, ry = SWAP_XY ? rawX : rawY;
  x = mapAxis(rx, RAW_X_MIN, RAW_X_MAX, w_, FLIP_X);
  y = mapAxis(ry, RAW_Y_MIN, RAW_Y_MAX, h_, FLIP_Y);
#if RP2COMMON_TOUCH_LOG
  std::printf("touch: raw %d %d -> %d %d\n", rawX, rawY, x, y);
#endif
  return true;
}

}  // namespace rp2common
