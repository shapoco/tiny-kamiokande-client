#ifndef TKRP2_CONSOLE_HPP
#define TKRP2_CONSOLE_HPP

// シリアルコンソール (USB CDC)。ESP-IDF 版の serial.cpp と同じコマンド:
//   help    コマンド一覧
//   wifi    SSID、パスフレーズ、IPv4 アドレス (空なら DHCP)、サブネットマスク、ゲートウェイ、DNS を順に聞く
//           (Enter で今の値のまま、'-' で消す)
//   status  WiFi の状態、IP アドレス、取得の状況、メモリ
//   clear   認証情報を消す
// タスクが無いので、フレームループから consolePoll() を呼んで届いた文字だけを処理する (待たない)

#include "tkapp/app.hpp"

namespace tkrp2 {

struct ConsoleHooks {
  // 設定を保存して接続し直す
  void (*apply)(const tkapp::WifiConfig &cfg);
  // 今の設定
  const tkapp::WifiConfig &(*current)();
  // status の最後に足す行 (メモリなど)
  void (*status)();
};

void consoleStart(const ConsoleHooks &hooks);
void consolePoll();

}  // namespace tkrp2

#endif
