#ifndef RP2COMMON_CLOCKS_HPP
#define RP2COMMON_CLOCKS_HPP

// RP2350 のオーバークロック。コア電圧、Flash (QMI) の分周、システムクロックの
// 順に設定し、clk_peri (SPI のクロック) を clk_sys から作り直す (SDK は起動後に
// システムクロックを変えると clk_peri を 48 MHz の USB PLL に移してしまう)。
// RP2040 (PicoSystem) では Flash の分周は boot2 のまま (ボードヘッダの
// PICO_FLASH_SPI_CLKDIV) で、QMI の項目は SSI の分周を報告するだけ。

#include <cstddef>
#include <cstdint>

namespace rp2common {

struct ClockConfig {
  uint32_t sysKhz;   // システムクロック (kHz)
  int vreg;          // コア電圧 (VREG_VOLTAGE_*)
  int flashClkdiv;   // Flash のクロック = clk_sys / n
  int flashRxdelay;  // QMI のサンプリング遅延 (clk_sys の半周期単位)
  int periDiv;       // clk_peri = clk_sys / n (1..3)
};

// 既定は 300 MHz、1.30 V。Flash は 300 / 3 = 100 MHz (W25Q128JV、W25Q32RV は
// 133 MHz まで。ボードヘッダで RP2COMMON_FLASH_CLKDIV を変えられる。分周 2 の 150 MHz は
// 定格超えだが、PicoLibSDK のローダーが残した設定で PicoPad 2 がそのまま動いていた)。
// clk_peri は 300 / 2 = 150 MHz で、SPI はそれを 2 分周した 75 MHz になる。
// clk_peri は clk_sys の整数分の 1、SPI はさらに偶数分の 1 なので、300 MHz では
// 62.5 MHz は作れない (75 か 50 MHz)。
#ifndef RP2COMMON_SYS_KHZ
#define RP2COMMON_SYS_KHZ 300000
#endif
#ifndef RP2COMMON_VREG
#define RP2COMMON_VREG VREG_VOLTAGE_1_30
#endif
#ifndef RP2COMMON_FLASH_CLKDIV
#define RP2COMMON_FLASH_CLKDIV 3
#endif
#ifndef RP2COMMON_FLASH_RXDELAY
#define RP2COMMON_FLASH_RXDELAY 2
#endif
#ifndef RP2COMMON_PERI_DIV
#define RP2COMMON_PERI_DIV 2
#endif

ClockConfig defaultClockConfig();

// main() の最初、stdio と core1 の起動より前に呼ぶ。システムクロックを
// 設定できなければ false (元のまま)
bool initClocks(const ClockConfig &cfg);

// initClocks() が見つけた値と設定した値 (ログ用)
struct ClockReport {
  uint32_t qmiTimingBefore, qmiTimingAfter;
  uint32_t sysHz, periHz;
};
const ClockReport &clockReport();

// Flash の品種と XIP の読み出し設定 (ログ用)。JEDEC ID は Flash に直接コマンドを
// 送って読むので、core1 を起こす前に呼ぶ
struct FlashReport {
  uint8_t jedec[3];       // メーカー、種類、容量
  uint32_t rfmt, rcmd;    // QMI の読み出しフォーマットとコマンド
};
FlashReport flashReport();

// FlashReport を 1 行にする (例: "jedec ef 40 18, read cmd eb quad addr, 4 dummy")
void formatFlashReport(const FlashReport &r, char *buf, size_t bytes);

// XIP (QMI と QSPI のパッド) を起動経路に関わらず同じ設定にする。SDK の flash_do_cmd() や
// 消去・書き込みは最後に BOOTRAM の XIP 復元コードを実行し、その中身は起動経路で決まる
// (通常起動: bootrom が SFDP から選んだ設定。PicoCalc の UF2 Loader: 03h の 1 線と分周 6。
// PicoPad 2 の PicoLibSDK のローダー: Continuous Read と分周 2)。initClocks() の分周も
// それで上書きされる。
// Puya と Winbond では QE ビットを立てて EBh Quad I/O の Continuous Read (SDK の boot2 と
// 同じ) にし、他の品種では読み出しフォーマットはそのまま、分周とパッドだけを設定する。
// core1 を起こす前に、flashReport() の後に呼ぶ。RP2040 では何もしない (boot2 が XIP を戻す)
void configureXip(const FlashReport &r);

// configureXip() が決めた設定を書き直す。SDK の消去・書き込みの後に、割り込みを止めた
// RAM 上のコードから呼ぶ (Flash を読める状態に戻るまで Flash 上の命令を読まないため)
void restoreXip();

}  // namespace rp2common

#endif
