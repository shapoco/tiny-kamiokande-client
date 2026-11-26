#ifndef TKRP2_RUNNER_HPP
#define TKRP2_RUNNER_HPP

// Pico SDK (RP2350 + CYW43439) 版のフレームループ。ESP-IDF 版の tk_runner と
// texbreakout の rp2common/runner.cpp に倣う。
//
// フレームは帯 (stripH 行) に分けて描く。帯の上半分を core0、下半分を core1 が同時に描き、
// 前の帯を DMA で送っている間に次の帯を描く。core1 は WiFi (CYW43) と lwIP の割り込みも受け持つ
// (net.hpp)。データ取得 (pipeline.hpp)、シリアル (console.hpp) は core0 のフレームの合間に回す。
//
// パネルのピンと種類はボードヘッダ (boards/) の RP2COMMON_* から取る。入力デバイスは Board で与える。

#include <cstdint>

#include "panel_spi.hpp"
#include "tkapp/app.hpp"

namespace tkrp2 {

struct Config {
  const char *name;       // ログ用
  int stripH;             // 帯の高さ [行]
  bool multiTouch;        // ピンチでズームできる (false ならダブルタップと長押し)
  bool demo;              // 通信せず埋め込みのサンプルを順に表示する
  float demoIntervalSec;  // サンプルを切り替える間隔
};

// ボードごとの入力。全て core0 から呼ばれる
class Board {
 public:
  virtual ~Board() = default;

  // パネルの初期化の後 (入力デバイス)
  virtual void initDevices() {}

  // 毎フレーム 1 回、前のフレームの最後の帯を送り終えてパネルの SPI が空いたとき
  // (最初の帯を送る前) に呼ばれる。パネルと SPI を共用するデバイス (抵抗膜タッチ) はここで読む
  virtual void onPanelIdle() {}

  // 毎フレーム、advance() の前に入力をアプリに渡す
  virtual void poll(tkapp::App &app) = 0;

  // 起動時のログの後に、デバイスの状態などを出す
  virtual void log() {}
};

// ボードを起こし (クロック、パネル、Board、core1 と WiFi)、フレームループを回す。戻らない
[[noreturn]] void run(Board &board, const Config &cfg);

rp2common::SpiPanel &panel();

}  // namespace tkrp2

#endif
