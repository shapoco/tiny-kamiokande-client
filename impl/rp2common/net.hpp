#ifndef TKRP2_NET_HPP
#define TKRP2_NET_HPP

// WiFi (CYW43439、STA) と HTTPS の GET (Pico 2 W)。ESP-IDF 版の net.cpp に当たる。
//
// lwIP と CYW43 のドライバは pico_cyw43_arch_lwip_threadsafe_background で、通信の処理は
// netInitOnThisCore() を呼んだコア (core1) の割り込みで動く。TLS の握手の計算もそこで行うので、
// その間は core1 の描画が遅れる。握手を減らすため接続は keep-alive で使い回し、切れたら
// セッションチケットで短い握手で繋ぎ直す。
// netInitOnThisCore() 以外は core0 (フレームループ) から呼ぶ。

#include <cstddef>
#include <cstdint>

#include "tkapp/app.hpp"

namespace tkrp2 {

// CYW43 を起こし、STA にして TLS の設定を作る。割り込みを受け持つコア (core1) で 1 回だけ呼ぶ
bool netInitOnThisCore();

// 毎フレーム呼ぶ。接続の状態を見て、切れていれば繋ぎ直す
void netPoll(uint64_t nowUs);

// 設定を変えて接続し直す (SSID が空なら切断する)。保存は呼び出し側
void netSetConfig(const tkapp::WifiConfig &cfg);
tkapp::WifiState netState();
// 接続中なら IP アドレスの文字列、そうでなければ ""
const char *netIpString();

// --- HTTPS の GET (1 度に 1 件) ---
enum class HttpState : uint8_t { IDLE, BUSY, DONE, FAILED };

struct HttpResult {
  size_t bytes;        // 本文のバイト数
  int status;          // HTTP の応答コード (届かなければ 0)
  int64_t sentUs;      // 要求を送り終えた時刻 (time_us_64)
  int64_t headersUs;   // 応答のヘッダを受け取った時刻
  bool reused;         // keep-alive の接続を使い回した
  bool resumed;        // 新しい接続で、セッションチケットで握手を短くした
  uint32_t handshakeUs;  // 新しい接続を作った時間 (DNS、TCP、TLS)。使い回したら 0
};

// url (https://host/path) を buf に読み始める。BUSY なら false
bool httpStart(const char *url, uint8_t *buf, size_t cap, uint64_t nowUs);
// 進み具合。DONE か FAILED になったら httpResult() を読み、httpFinish() で IDLE に戻す
HttpState httpPoll(uint64_t nowUs);
const HttpResult &httpResult();
void httpFinish();

}  // namespace tkrp2

#endif
