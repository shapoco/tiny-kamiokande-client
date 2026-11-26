#ifndef TKRP2_SETTINGS_STORE_HPP
#define TKRP2_SETTINGS_STORE_HPP

// WiFi の設定 (tkapp::WifiConfig) を Flash の最後のセクタに置く (ESP-IDF 版の NVS の代わり)。
// 印と CRC を付けて 1 ページに書く。書いている間は割り込みと XIP が止まるので、
// もう一方のコアは RAM 上で待たせておくこと (runner.cpp の saveSettings())

#include "tkapp/app.hpp"

namespace tkrp2 {

// 保存されていなければ (または壊れていれば) false
bool settingsLoad(tkapp::WifiConfig &cfg);

// セクタを消して書き、読み戻して確かめる。消去に数十〜数百 ms かかる
bool settingsSave(const tkapp::WifiConfig &cfg);

}  // namespace tkrp2

#endif
