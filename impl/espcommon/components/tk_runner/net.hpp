#ifndef TK_NET_HPP
#define TK_NET_HPP

// WiFi (STA) と HTTPS 取得。認証情報は NVS に置く。どのタスクから呼んでもよい

#include <cstddef>
#include <cstdint>

#include "tkapp/app.hpp"

namespace tkrunner {

// NVS から認証情報を読む。無ければ false
bool netLoadConfig(tkapp::WifiConfig &cfg);
// 保存して (再) 接続する
void netApplyConfig(const tkapp::WifiConfig &cfg);
// 認証情報を消して切断する
void netClearConfig();
// 起動時に 1 回。保存された認証情報があれば接続を始める
void netStart();
tkapp::WifiState netState();
// 接続中なら IP アドレスの文字列 ("192.168.0.2")、そうでなければ ""
const char *netIpString();
// 設定が (シリアルなどで) 変わっていれば cfg に入れて true (描画側が App に反映する)
bool netTakeUpdatedConfig(tkapp::WifiConfig &cfg);

// URL を GET して buf に入れる。戻り値はバイト数 (失敗なら負)。httpStatus に応答コード
// timing を渡すと、要求を送り終えた時刻とヘッダを受け取った時刻 (esp_timer_get_time) を書く (サーバ時刻とのずれの推定用)
struct NetTiming {
  int64_t sentUs = 0, headersUs = 0;
};
int netFetch(const char *url, uint8_t *buf, size_t cap, int *httpStatus, NetTiming *timing = nullptr);

}  // namespace tkrunner

#endif
