#ifndef TK_SERIAL_HPP
#define TK_SERIAL_HPP

// シリアルコンソール (USB Serial/JTAG または UART)。対話的に WiFi の設定をする。
//   help    コマンド一覧
//   wifi    SSID、パスフレーズ、IPv4 アドレス (空なら DHCP)、サブネットマスク、ゲートウェイ、DNS を順に聞く
//   status  WiFi の状態、IP アドレス、取得と推定の状況
//   clear   認証情報を消す
//   hist    履歴の時間軸の一覧。hist N で N 番目を選ぶ (タップと同じ)
//   live    LIVE に戻る

namespace tkrunner {

// 読み取りのタスクを起動する (core は置くコア)
void serialStart(int core);

// hist / live コマンドの要求を描画側が受け取る (毎フレーム)。戻り値: -3 なし、-2 live、-1 一覧、0.. 選ぶ添字
int serialTakeHistoryCommand();

// 機種固有のコマンドを足す (serialStart() の前に。最大 4 つ)。fn は読み取りのタスクで呼ばれる
void serialAddCommand(const char *name, const char *help, void (*fn)());

}  // namespace tkrunner

// C から呼ぶ版 (tkrunner::serialAddCommand と同じ)
extern "C" void tkSerialAddCommand(const char *name, const char *help, void (*fn)(void));

#endif
