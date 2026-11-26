#include "clocks.hpp"

#include <algorithm>
#include <cstdio>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/structs/xip_ctrl.h"
#if PICO_RP2350
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/qmi.h"
#else
#include "hardware/structs/ssi.h"
#endif
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

#ifndef RP2COMMON_FLASH_QUAD
#define RP2COMMON_FLASH_QUAD 1
#endif

namespace rp2common {

namespace {

ClockReport g_report;

#if PICO_RP2350
// 03h (1 線、ダミー無し) の読み出しは速いクロックに対応しない (W25Q32 で 50 MHz まで) ので、
// 300 MHz / 6 = 50 MHz に抑える (起動時に 03h だったとき。flash_do_cmd() が 03h に戻す
// 場合は flashDoCmd() を参照)
constexpr uint8_t CMD_READ_DATA = 0x03;
constexpr uint32_t SERIAL_READ_CLKDIV = 6;

// Quad I/O (または bootrom が選んだ速い読み出し) での分周
uint32_t g_fastClkdiv = 0, g_rxdelay = 0;

uint32_t timingWith(uint32_t t, uint32_t clkdiv, uint32_t rxdelay) {
  t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
  t |= (clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS;
  t |= (rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS;
  return t;
}

// Flash のタイミングを変える間に Flash から命令を読まないよう、RAM 上で
// 割り込みを止めて行う
void __no_inline_not_in_flash_func(setFlashTiming)(uint32_t timing) {
  const uint32_t irq = save_and_disable_interrupts();
  qmi_hw->m[0].timing = timing;
  __dsb();
  __isb();
  restore_interrupts(irq);
}
#endif  // PICO_RP2350

// flash_do_cmd() で Flash にコマンドを送る。XIP を止める間、Flash 上の割り込みハンドラ
// (USB、音声の DMA) が走らないようにする。flash_do_cmd() は最後に XIP を BOOTRAM の復元コードの
// 設定 (起動経路で決まる) に戻すので、呼んだ後は configureXip() で設定し直す
void flashDoCmd(uint8_t *tx, uint8_t *rx, size_t n) {
  const uint32_t irq = save_and_disable_interrupts();
  flash_do_cmd(tx, rx, n);
  restore_interrupts(irq);
}

}  // namespace

ClockConfig defaultClockConfig() {
  return {RP2COMMON_SYS_KHZ, RP2COMMON_VREG, RP2COMMON_FLASH_CLKDIV, RP2COMMON_FLASH_RXDELAY,
          RP2COMMON_PERI_DIV};
}

bool initClocks(const ClockConfig &cfg) {
  // 電圧を上げ、Flash を遅くしてから、クロックを上げる
  vreg_set_voltage((enum vreg_voltage)cfg.vreg);
  sleep_ms(10);
#if PICO_RP2350
  g_report.qmiTimingBefore = qmi_hw->m[0].timing;
  g_fastClkdiv = (uint32_t)cfg.flashClkdiv;
  g_rxdelay = (uint32_t)cfg.flashRxdelay;
  const bool serialRead =
      ((qmi_hw->m[0].rcmd & QMI_M0_RCMD_PREFIX_BITS) >> QMI_M0_RCMD_PREFIX_LSB) == CMD_READ_DATA;
  const uint32_t clkdiv =
      serialRead && g_fastClkdiv < SERIAL_READ_CLKDIV ? SERIAL_READ_CLKDIV : g_fastClkdiv;
  setFlashTiming(timingWith(qmi_hw->m[0].timing, clkdiv, g_rxdelay));
  g_report.qmiTimingAfter = qmi_hw->m[0].timing;
#else
  // RP2040 の Flash (SSI) の分周は boot2 がボードヘッダの PICO_FLASH_SPI_CLKDIV で設定する
  g_report.qmiTimingBefore = g_report.qmiTimingAfter = ssi_hw->baudr;
#endif
  const bool ok = set_sys_clock_khz(cfg.sysKhz, false);
  const uint32_t sysHz = clock_get_hz(clk_sys);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sysHz,
                  sysHz / (uint32_t)cfg.periDiv);
  g_report.sysHz = sysHz;
  g_report.periHz = clock_get_hz(clk_peri);
  return ok;
}

const ClockReport &clockReport() { return g_report; }

FlashReport flashReport() {
  FlashReport r = {};
  // JEDEC ID (9Fh): コマンド 1 バイトの後に 3 バイト返る
  uint8_t tx[4] = {0x9F, 0, 0, 0}, rx[4] = {};
  flashDoCmd(tx, rx, sizeof tx);
  for (int i = 0; i < 3; i++) r.jedec[i] = rx[i + 1];
#if PICO_RP2350
  r.rfmt = qmi_hw->m[0].rfmt;
  r.rcmd = qmi_hw->m[0].rcmd;
#else
  // RP2040: SSI の設定 (boot2 が決める)
  r.rfmt = ssi_hw->spi_ctrlr0;
  r.rcmd = ssi_hw->baudr;
#endif
  return r;
}

#if PICO_RP2350

namespace {

constexpr uint8_t JEDEC_PUYA = 0x85, JEDEC_WINBOND = 0xEF;
constexpr uint8_t CMD_WRITE_ENABLE = 0x06, CMD_READ_STATUS1 = 0x05, CMD_READ_STATUS2 = 0x35,
                  CMD_WRITE_STATUS = 0x01, CMD_WRITE_STATUS2 = 0x31;
constexpr uint8_t SR1_BUSY = 0x01, SR2_QE = 0x02;

// EBh (Quad I/O) と、アドレスに続くモードビット A0h (Continuous Read: 以後のバーストは
// コマンドを送らずアドレスから始める)
constexpr uint8_t CMD_QUAD_READ = 0xEB, MODE_CONTINUOUS_READ = 0xA0;
constexpr uint32_t FLASH_COOLDOWN = 1;  // 連続アクセスで CS を保つ時間 (SDK の boot2 と同じ)
constexpr uint32_t PREFIX_LEN_8 = QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB;
constexpr uint32_t MIN_DESELECT_MAX = QMI_M0_TIMING_MIN_DESELECT_BITS >> QMI_M0_TIMING_MIN_DESELECT_LSB;

// CS を解放してから次に選択するまでの最短時間 (データシートの tSHSL、読み出しから読み出し)。
// QMI の解放は SCK の半周期 (切り上げ) + MIN_DESELECT サイクルで、半周期だけでは Puya の
// 20 ns に足りず XIAO RP2350 が起動しなかった (実機で確認)
constexpr uint32_t DESELECT_NS_WINBOND = 10, DESELECT_NS_PUYA = 20, DESELECT_NS_UNKNOWN = 50;

// 目標の設定。configureXip() が決め、applyXip() が書く
uint32_t g_xipRcmd = 0, g_xipRfmt = 0, g_xipTiming = 0;
bool g_xipValid = false;

// CS の最大保持とページ境界での分割は制約しない (SDK の boot2 と同じ)
uint32_t flashTiming(uint32_t clkdiv, uint32_t rxdelay, uint32_t deselectNs) {
  const uint32_t sysHz = clock_get_hz(clk_sys);
  const uint32_t halfSck = (clkdiv + 1) / 2;
  const uint32_t deselect = (uint32_t)(((uint64_t)deselectNs * sysHz + 999999999u) / 1000000000u);
  const uint32_t minDeselect = std::min(deselect > halfSck ? deselect - halfSck : 0, MIN_DESELECT_MAX);
  return (FLASH_COOLDOWN << QMI_M0_TIMING_COOLDOWN_LSB) |
         (minDeselect << QMI_M0_TIMING_MIN_DESELECT_LSB) |
         ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS) |
         ((clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS);
}

// プレフィックス無し = Continuous Read に入っている (入れた側が必ずこうする)
bool isContinuous(uint32_t rfmt) {
  return ((rfmt & QMI_M0_RFMT_PREFIX_LEN_BITS) >> QMI_M0_RFMT_PREFIX_LEN_LSB) ==
         QMI_M0_RFMT_PREFIX_LEN_VALUE_NONE;
}

uint8_t readCommand(uint32_t rcmd) { return (rcmd & QMI_M0_RCMD_PREFIX_BITS) >> QMI_M0_RCMD_PREFIX_LSB; }

void flashCommand(uint8_t *buf, size_t n) { flashDoCmd(buf, buf, n); }

uint8_t readStatus(uint8_t cmd) {
  uint8_t buf[2] = {cmd, 0};
  flashCommand(buf, sizeof buf);
  return buf[1];
}

// QE (ステータスレジスタ 2 のビット 1) を立てる。不揮発なので立っていれば書かない。
// Puya は 01h で SR1 と SR2 を続けて、Winbond は 31h で SR2 だけを書く
bool enableQuad(bool puya) {
  if (readStatus(CMD_READ_STATUS2) & SR2_QE) return true;
  const uint8_t sr1 = readStatus(CMD_READ_STATUS1);
  uint8_t wren[1] = {CMD_WRITE_ENABLE};
  flashCommand(wren, sizeof wren);
  if (puya) {
    uint8_t wrsr[3] = {CMD_WRITE_STATUS, sr1, SR2_QE};
    flashCommand(wrsr, sizeof wrsr);
  } else {
    uint8_t wrsr2[2] = {CMD_WRITE_STATUS2, SR2_QE};
    flashCommand(wrsr2, sizeof wrsr2);
  }
  for (int i = 0; i < 100 && (readStatus(CMD_READ_STATUS1) & SR1_BUSY); i++) sleep_ms(1);
  return readStatus(CMD_READ_STATUS2) & SR2_QE;
}

// QMI と QSPI のパッドを目標の設定にする。Flash から命令を読まないよう RAM で、
// 割り込みを止めて行う
void __no_inline_not_in_flash_func(applyXip)() {
  const uint32_t irq = save_and_disable_interrupts();
  // SCLK は 8 mA の速い立ち上がりで入力バッファ無し、SDx はシュミットトリガを切って遅延を
  // 減らす (SDK の boot2 と同じ。bootrom と flash_exit_xip() は 4 mA の遅い設定にする)
  pads_qspi_hw->io[0] =
      (2u << PADS_QSPI_GPIO_QSPI_SCLK_DRIVE_LSB) | PADS_QSPI_GPIO_QSPI_SCLK_SLEWFAST_BITS;
  for (int i = 1; i <= 4; i++) hw_clear_bits(&pads_qspi_hw->io[i], PADS_QSPI_GPIO_QSPI_SD0_SCHMITT_BITS);
  const bool chipContinuous = isContinuous(qmi_hw->m[0].rfmt);
  qmi_hw->m[0].timing = g_xipTiming;
  qmi_hw->m[0].rcmd = g_xipRcmd;
  if (isContinuous(g_xipRfmt) && !chipContinuous) {
    // 1 回だけコマンドとモードビットを送って Continuous Read に入れる
    qmi_hw->m[0].rfmt = g_xipRfmt | PREFIX_LEN_8;
    (void)*(volatile uint8_t *)XIP_NOCACHE_NOALLOC_BASE;
  }
  qmi_hw->m[0].rfmt = g_xipRfmt;
  (void)*(volatile uint8_t *)XIP_NOCACHE_NOALLOC_BASE;
  __dsb();
  __isb();
  restore_interrupts(irq);
}

}  // namespace

void configureXip(const FlashReport &r) {
  uint32_t rcmd = qmi_hw->m[0].rcmd, rfmt = qmi_hw->m[0].rfmt;
  const bool puya = r.jedec[0] == JEDEC_PUYA, winbond = r.jedec[0] == JEDEC_WINBOND;
  const uint32_t deselectNs =
      puya ? DESELECT_NS_PUYA : winbond ? DESELECT_NS_WINBOND : DESELECT_NS_UNKNOWN;
#if RP2COMMON_FLASH_QUAD
  // 確認した品種だけ
  if ((puya || winbond) && enableQuad(puya)) {
    // コマンド 1 線、アドレス・モードビット・ダミー 4 クロック (16 ビット)・データは 4 線
    rcmd = ((uint32_t)CMD_QUAD_READ << QMI_M0_RCMD_PREFIX_LSB) |
           ((uint32_t)MODE_CONTINUOUS_READ << QMI_M0_RCMD_SUFFIX_LSB);
    rfmt = (QMI_M0_RFMT_PREFIX_WIDTH_VALUE_S << QMI_M0_RFMT_PREFIX_WIDTH_LSB) |
           (QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB) |
           (QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB) |
           (QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB) |
           (QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB) |
           (QMI_M0_RFMT_PREFIX_LEN_VALUE_NONE << QMI_M0_RFMT_PREFIX_LEN_LSB) |
           (QMI_M0_RFMT_SUFFIX_LEN_VALUE_8 << QMI_M0_RFMT_SUFFIX_LEN_LSB) |
           (QMI_M0_RFMT_DUMMY_LEN_VALUE_16 << QMI_M0_RFMT_DUMMY_LEN_LSB);
  }
#endif
  // 03h のままなら速いクロックに対応しない
  const bool serialRead = readCommand(rcmd) == CMD_READ_DATA;
  const uint32_t clkdiv =
      serialRead && g_fastClkdiv < SERIAL_READ_CLKDIV ? SERIAL_READ_CLKDIV : g_fastClkdiv;
  g_xipRcmd = rcmd;
  g_xipRfmt = rfmt;
  g_xipTiming = flashTiming(clkdiv, g_rxdelay, deselectNs);
  g_xipValid = true;
  applyXip();
}

void __no_inline_not_in_flash_func(restoreXip)() {
  if (g_xipValid) applyXip();
}

void formatFlashReport(const FlashReport &r, char *buf, size_t bytes) {
  // 幅: 0 = 1 線、1 = 2 線、2 = 4 線
  static const char *const WIDTH[] = {"single", "dual", "quad", "?"};
  const uint32_t f = r.rfmt;
  const int prefixLen = (f >> QMI_M0_RFMT_PREFIX_LEN_LSB) & 1;
  const int suffixLen = (f >> QMI_M0_RFMT_SUFFIX_LEN_LSB) & 3;
  const int dummyLen = (f >> QMI_M0_RFMT_DUMMY_LEN_LSB) & 7;
  const int addrWidth = (f >> QMI_M0_RFMT_ADDR_WIDTH_LSB) & 3;
  const int dataWidth = (f >> QMI_M0_RFMT_DATA_WIDTH_LSB) & 3;
  // DUMMY_LEN は 0 で無し、n で 4n ビット
  std::snprintf(buf, bytes,
                "jedec %02x %02x %02x, read cmd %02lx%s, addr %s, data %s, %d dummy bits%s",
                r.jedec[0], r.jedec[1], r.jedec[2],
                (unsigned long)((r.rcmd >> QMI_M0_RCMD_PREFIX_LSB) & 0xFF),
                prefixLen ? "" : " (no prefix: continuous read)", WIDTH[addrWidth],
                WIDTH[dataWidth], dummyLen * 4, suffixLen ? " + mode bits" : "");
}

#else  // RP2040

// boot2 (W25Q080 用) が EBh の Quad I/O の Continuous Read にし、SDK が Flash の操作の後に
// boot2 を呼び直して戻す
void configureXip(const FlashReport &r) { (void)r; }
void restoreXip() {}

void formatFlashReport(const FlashReport &r, char *buf, size_t bytes) {
  // SPI_CTRLR0: TRANS_TYPE (ビット 0-1)、INST_L (8-9)、ADDR_L (2-5)、WAIT_CYCLES (11-15)、
  // XIP_CMD (24-31)。rcmd は SSI の分周
  const uint32_t f = r.rfmt;
  std::snprintf(buf, bytes,
                "jedec %02x %02x %02x, ssi xip cmd %02lx, trans type %lu, inst %lu bits, addr %lu "
                "bits, %lu wait cycles, clkdiv %lu",
                r.jedec[0], r.jedec[1], r.jedec[2], (unsigned long)(f >> 24),
                (unsigned long)(f & 3), (unsigned long)(((f >> 8) & 3) * 4),
                (unsigned long)(((f >> 2) & 0xF) * 4), (unsigned long)((f >> 11) & 0x1F),
                (unsigned long)r.rcmd);
}

#endif  // PICO_RP2350

}  // namespace rp2common
