#ifndef RP2COMMON_PANEL_SPI_HPP
#define RP2COMMON_PANEL_SPI_HPP

// 4 線 SPI のパネル (ST7789 系、ST7365P) に帯を DMA で書く。
//
// 帯ごとに描く範囲 (パネル中央の frameW x frameH) の中の窓を開き (CASET /
// RASET / RAMWR)、画素を DMA で送る。帯のバッファは RGB565_SWAPPED の 2 枚で、
// 片方を描いている間にもう片方を送る。
// CS はコマンドと画素の転送ごとに Low にし、終わったら High に戻す (Xiamocon
// SDK と同じ。転送の切れ目でパネルのバイト境界が同期する)。DC はコマンドの
// バイトの間だけ Low にし、他は High に保つ (Xiamocon は CS か DC が High で
// ないと周辺の電源が落ちる)。

#include <cstdint>

#include "hardware/spi.h"

namespace rp2common {

// 初期化コマンド列の種類
enum class PanelKind : uint8_t {
  ST7789T3,  // Waveshare RP2350-Touch-LCD-2 (Waveshare のデモの設定)
  ST7365P,   // PicoCalc (ClockworkPi の設定)
  ST7789,    // Xiamocon (Xiamocon SDK の設定: 最小限)
  ST7789VW,  // Waveshare Pico-ResTouch-LCD-2.8 (Waveshare のデモの設定。反転表示なし)
  ST7789_PICOSYSTEM,  // PicoSystem (PicoSystem SDK の設定と順序)
};

struct PanelConfig {
  spi_inst_t *spi;
  int sckPin, mosiPin, csPin, dcPin;
  int rstPin;            // -1: リセットピン無し (呼び出し側が別の手段でリセットする)
  int blPin;             // -1: バックライトの制御無し
  int nativeW, nativeH;  // パネルの画素数
  int frameW, frameH;    // 描く範囲。パネルの中央に置く
  uint32_t baud;         // SPI クロック。clk_peri の偶数分の 1 になる
  uint32_t cmdBaud;      // コマンドを送るときの SPI クロック。0 なら baud と同じ
  PanelKind kind;
  uint8_t madctl;        // 縦向きの MADCTL (MX、BGR など)
  bool flip;             // 180 度回す (MADCTL の MY と MX を反転)
  int backlight;         // 0..255
};

class SpiPanel {
 public:
  // リセット (リセットピンが他のデバイスと共用なら、それらの初期化より前に呼ぶ)、
  // 設定、表示開始、stripH 行の帯バッファ 2 枚の確保。バッファが取れなければ false
  bool init(const PanelConfig &cfg, int stripH);

  // 描く範囲の大きさ
  int width() const { return cfg_.frameW; }
  int height() const { return cfg_.frameH; }
  uint32_t baud() const { return baud_; }

  uint16_t *buffer(int i) const { return buf_[i]; }

  // パネル全体を黒にする (ブロッキング)
  void clear();

  // バッファ i の先頭 rows 行を描く範囲の y 行目から送る。すぐ戻る。
  // drain() が戻るまでバッファに描いてはならない。drain() の後に呼ぶ
  void submit(int i, int y, int rows);

  // 送信中の帯の完了とバスのアイドルを待ち、CS を上げる。何度呼んでもよい
  void drain();

  void setBacklight(int level);  // 0..255。blPin が無ければ何もしない

  // 表示を止めてスリープに入れる (電源を切る前に)
  void displayOff();

 private:
  PanelConfig cfg_ = {};
  uint32_t baud_ = 0;
  uint32_t cmdBaud_ = 0;
  int dma_ = -1;
  bool pending_ = false;
  uint16_t *buf_[2] = {nullptr, nullptr};
  uint32_t settleCycles_ = 0;  // PIN_SETTLE_NS 分の clk_sys のサイクル数

  // CS と DC の切り替えの前後でパネルのセットアップ・ホールド時間を待つ
  void settle();
  void command(uint8_t cmd, const uint8_t *data = nullptr, int n = 0);
  // 窓を開いて RAMWR を送り、CS を Low のまま画素を待つ状態にする
  void beginPixels(int x, int y, int w, int h);
};

}  // namespace rp2common

#endif
