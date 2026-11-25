#ifndef TK_RUNNER_HPP
#define TK_RUNNER_HPP

// ESP-IDF 版のフレームループ。帯描画の仕組みは texbreakout / ShapoGFX の example/m5common/ に倣う。
// メインタスク (core0) がフレームを帯ごとに描いてパネルへ送り、データ取得は core1 の
// パイプライン (pipeline.hpp) が行う。ボードの起動、パネルへの転送、入力は Board で与える

#include <cstddef>
#include <cstdint>

#include "tkapp/app.hpp"

namespace tkrunner {

struct Config {
  const char *name;      // シリアルログ用
  int stripH;            // 帯の高さ [行]
  bool multiTouch;       // ピンチでズームできる
  bool demo;             // 通信せず埋め込みのサンプルを順に表示する
  float demoIntervalSec; // サンプルを切り替える間隔
};

// 帯の転送先。帯バッファ 2 枚を交互に描画と転送に使う
class PanelOut {
 public:
  virtual ~PanelOut() = default;
  // すぐ戻る。drain() が戻るまでバッファ i に描いてはならない
  virtual void submit(int i, int y, int rows) = 0;
  virtual void drain() = 0;
  uint16_t *buffer(int i) const { return buf_[i]; }

 protected:
  uint16_t *buf_[2] = {nullptr, nullptr};
};

// ボードごとの部分。全てメインタスクから呼ばれる
class Board {
 public:
  virtual ~Board() = default;
  // ボードの起動とパネル出力の準備 (帯バッファは min(cfg.stripH, 画面の高さ) 行)。フレームの大きさを返す
  virtual bool init(const Config &cfg, int &w, int &h) = 0;
  virtual PanelOut &panel() = 0;
  // 毎フレーム、advance() の前に入力をアプリに渡す
  virtual void poll(tkapp::App &app) = 0;
};

// ボードを起こし、パイプラインを起動してフレームループを回す。戻らない
[[noreturn]] void run(Board &board, const Config &cfg);

}  // namespace tkrunner

#endif
