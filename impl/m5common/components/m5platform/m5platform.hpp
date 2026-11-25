#ifndef M5PLATFORM_HPP
#define M5PLATFORM_HPP

// M5Stack 版のボード部分。M5Unified でボードを起動し、タッチを espcommon のフレームループ
// (tk_runner) につなぐ。各プロジェクトは設定だけを選ぶ

#include <cstddef>
#include <cstdint>

namespace m5platform {

struct Config {
  const char *name;      // シリアルログ用
  int rotation;          // M5.Display の回転 (縦向きの 0 か 2)
  int scale;             // フレームの 1 ピクセルあたりのパネルのピクセル数
  int stripH;
  bool demo;             // 通信せず埋め込みのサンプルを表示する
  float demoIntervalSec;
};

[[noreturn]] void run(const Config &cfg);

}  // namespace m5platform

#endif
