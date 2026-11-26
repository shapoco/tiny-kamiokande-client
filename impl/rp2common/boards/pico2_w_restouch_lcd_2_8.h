// Waveshare Pico-ResTouch-LCD-2.8 に Raspberry Pi Pico 2 W を載せたもの。
// SDK の pico2_w の設定に、ボードのデバイスのピンを RP2COMMON_* で足す
// (texbreakout の boards/pico_restouch_lcd_2_8.h の Pico 2 W 版)。
// ピンは Waveshare の回路図とデモ (https://www.waveshare.com/wiki/Pico-ResTouch-LCD-2.8) から。
//   LCD: 2.8 インチ 240x320 ST7789VW、SPI1。バックライトは GP13 (トランジスタ経由、High で点灯)
//   タッチ: XPT2046 (抵抗膜式、1 点)。SCK / MOSI / MISO をパネルと共用し、CS と PENIRQ だけ別
//   microSD: GP5、GP18-22 (SDIO 配線)。使わない
//   WiFi: CYW43439 は Pico 2 W の基板内の GP23 / 24 / 25 / 29 を使う (ResTouch のピンと重ならない)
//
//   set(PICO_BOARD pico2_w_restouch_lcd_2_8)
//   include(rp2common/pico_project.cmake)

#ifndef _BOARDS_PICO2_W_RESTOUCH_LCD_2_8_H
#define _BOARDS_PICO2_W_RESTOUCH_LCD_2_8_H

// CMake に読まれる値 (pico2_w.h のものと同じ。インクルードした先の分は読まれない)
pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (4 * 1024 * 1024))
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)

#define PICO2_W_RESTOUCH_LCD_2_8

#include "boards/pico2_w.h"

// --- パネル: ST7789VW、240x320 (縦向き) ---
#define RP2COMMON_LCD_SPI 1
#define RP2COMMON_LCD_SCK_PIN 10
#define RP2COMMON_LCD_MOSI_PIN 11
#define RP2COMMON_LCD_CS_PIN 9
#define RP2COMMON_LCD_DC_PIN 8
#define RP2COMMON_LCD_RST_PIN 15
#define RP2COMMON_LCD_BL_PIN 13
#define RP2COMMON_LCD_WIDTH 240
#define RP2COMMON_LCD_HEIGHT 320
#define RP2COMMON_LCD_KIND ST7789VW
#define RP2COMMON_LCD_MADCTL 0x00  // Waveshare のデモの縦向き (L2R_U2D) の値

// --- タッチ: XPT2046。パネルの SPI を借りる ---
#define RP2COMMON_TOUCH_MISO_PIN 12
#define RP2COMMON_TOUCH_CS_PIN 16
#define RP2COMMON_TOUCH_IRQ_PIN 17  // PENIRQ。押されていると Low

#endif
