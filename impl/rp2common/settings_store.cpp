#include "settings_store.hpp"

#include <cstring>

#include "clocks.hpp"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

// リンカスクリプトが置く、Flash 上のプログラムの終わり
extern "C" char __flash_binary_end;

namespace tkrp2 {
namespace {

constexpr uint32_t OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
constexpr uint32_t MAGIC = 0x544B5731u;  // "TKW1"

struct Record {
  uint32_t magic;
  uint32_t bytes;  // sizeof(tkapp::WifiConfig)
  tkapp::WifiConfig cfg;
  uint32_t crc;
};
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "the settings do not fit in a page");

alignas(4) uint8_t g_page[FLASH_PAGE_SIZE];

uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

bool available() { return (uintptr_t)&__flash_binary_end <= XIP_BASE + OFFSET; }

// SDK の消去と書き込みは最後に XIP を起動経路の設定に戻す (clocks.hpp の configureXip())。
// 設定し直すまで Flash を読まないよう、全体を RAM で動かす
void __no_inline_not_in_flash_func(eraseAndProgram)() {
  const uint32_t irq = save_and_disable_interrupts();
  flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(OFFSET, g_page, FLASH_PAGE_SIZE);
  rp2common::restoreXip();
  restore_interrupts(irq);
}

}  // namespace

bool settingsLoad(tkapp::WifiConfig &cfg) {
  if (!available()) return false;
  Record r;
  std::memcpy(&r, (const void *)(XIP_BASE + OFFSET), sizeof r);
  if (r.magic != MAGIC || r.bytes != sizeof(tkapp::WifiConfig)) return false;
  if (crc32((const uint8_t *)&r, offsetof(Record, crc)) != r.crc) return false;
  r.cfg.ssid[tkapp::SSID_MAX] = 0;
  r.cfg.pass[tkapp::PASS_MAX] = 0;
  r.cfg.ip[tkapp::IPV4_MAX] = r.cfg.mask[tkapp::IPV4_MAX] = 0;
  r.cfg.gateway[tkapp::IPV4_MAX] = r.cfg.dns[tkapp::IPV4_MAX] = 0;
  cfg = r.cfg;
  return true;
}

bool settingsSave(const tkapp::WifiConfig &cfg) {
  if (!available()) return false;
  Record r = {};
  r.magic = MAGIC;
  r.bytes = sizeof(tkapp::WifiConfig);
  r.cfg = cfg;
  r.crc = crc32((const uint8_t *)&r, offsetof(Record, crc));
  std::memset(g_page, 0xFF, sizeof g_page);
  std::memcpy(g_page, &r, sizeof r);
  eraseAndProgram();
  return std::memcmp((const void *)(XIP_BASE + OFFSET), &r, sizeof r) == 0;
}

}  // namespace tkrp2
