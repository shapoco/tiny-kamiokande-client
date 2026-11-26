#include "net.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "mbedtls/ssl.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "root_certs.h"

namespace tkrp2 {

namespace {

constexpr uint64_t RECONNECT_DELAY_US = 3000000;
constexpr uint64_t FAILED_AFTER_US = 30000000;  // 接続できないままこの時間で FAILED 表示 (再接続は続ける)
constexpr uint64_t JOIN_TIMEOUT_US = 20000000;  // join を始めてからこの時間つながらなければやり直す
constexpr uint64_t POLL_INTERVAL_US = 100000;
constexpr uint64_t HTTP_TIMEOUT_US = 15000000;
constexpr uint16_t HTTPS_PORT = 443;

// lwIP と CYW43 のドライバの排他 (割り込みのコアと共有する状態はすべてこの中で触る)
struct Lock {
  Lock() { cyw43_arch_lwip_begin(); }
  ~Lock() { cyw43_arch_lwip_end(); }
};

// ---------------------------------------------------------------- WiFi

bool g_ready = false;  // netInitOnThisCore() が成功した
tkapp::WifiConfig g_cfg;
bool g_haveCfg = false;
volatile bool g_connectPending = false;  // netSetConfig() から netPoll() へ
bool g_joinIssued = false;
tkapp::WifiState g_state = tkapp::WifiState::NO_CONFIG;
uint64_t g_downSinceUs = 0;  // つながっていない状態が始まった時刻 (FAILED の判定)
uint64_t g_attemptUs = 0;    // 最後に join を始めた時刻
uint64_t g_retryAtUs = 0;    // 0 でなければこの時刻に join をやり直す
uint64_t g_lastPollUs = 0;
char g_ip[16] = "";

// ---------------------------------------------------------------- HTTPS

struct altcp_tls_config *g_tls = nullptr;
struct altcp_pcb *g_pcb = nullptr;
bool g_pcbReady = false;  // TLS の握手が済み、要求を送れる
struct altcp_tls_session g_session;
bool g_haveSession = false;

enum class Parse : uint8_t { STATUS, HEADER, BODY_LEN, BODY_CLOSE, CHUNK_SIZE, CHUNK_DATA, CHUNK_END, TRAILER, DONE };

struct Request {
  HttpState state = HttpState::IDLE;
  char host[64];
  char path[192];
  uint8_t *buf;
  size_t cap, len;
  Parse parse;
  char line[128];
  size_t lineLen;
  int status;
  bool chunked, close;
  long contentLength;
  size_t remain;
  uint64_t startUs, connectStartUs;
  bool gotBytes;       // この要求で何か受け取った
  bool retried;        // 使い回した接続が切れていたので繋ぎ直した
  bool retryPending;   // 割り込みから netPoll() へ: 繋ぎ直して送り直す
  HttpResult result;
};
Request g_req;

void resetParse() {
  g_req.parse = Parse::STATUS;
  g_req.lineLen = 0;
  g_req.len = 0;
  g_req.status = 0;
  g_req.chunked = g_req.close = false;
  g_req.contentLength = -1;
  g_req.remain = 0;
  g_req.gotBytes = false;
}

void finish(bool ok) {
  g_req.result.bytes = g_req.len;
  g_req.result.status = g_req.status;
  g_req.state = ok ? HttpState::DONE : HttpState::FAILED;
}

// 接続を閉じる。閉じられなければ中止する。中止したら true (lwIP のコールバックからは ERR_ABRT を返す)
bool closePcb(bool abort) {
  if (!g_pcb) return false;
  struct altcp_pcb *pcb = g_pcb;
  g_pcb = nullptr;
  g_pcbReady = false;
  altcp_arg(pcb, nullptr);
  altcp_recv(pcb, nullptr);
  altcp_err(pcb, nullptr);
  if (!abort && altcp_close(pcb) == ERR_OK) return false;
  altcp_abort(pcb);
  return true;
}

bool parseUrl(const char *url, char *host, size_t hostCap, char *path, size_t pathCap) {
  static const char PREFIX[] = "https://";
  if (strncmp(url, PREFIX, sizeof PREFIX - 1) != 0) return false;
  const char *h = url + sizeof PREFIX - 1;
  const char *slash = strchr(h, '/');
  const size_t hl = slash ? (size_t)(slash - h) : strlen(h);
  if (hl == 0 || hl >= hostCap) return false;
  memcpy(host, h, hl);
  host[hl] = 0;
  const char *p = slash ? slash : "/";
  if (strlen(p) >= pathCap) return false;
  strcpy(path, p);
  return true;
}

bool startsWithNoCase(const char *s, const char *prefix) {
  for (; *prefix; s++, prefix++) {
    char c = *s;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != *prefix) return false;
  }
  return true;
}

bool containsNoCase(const char *s, const char *word) {
  for (; *s; s++)
    if (startsWithNoCase(s, word)) return true;
  return false;
}

// ヘッダとチャンクの大きさの行を 1 行処理する
void handleLine() {
  char *l = g_req.line;
  switch (g_req.parse) {
    case Parse::STATUS: {
      const char *sp = strchr(l, ' ');
      g_req.status = sp ? atoi(sp + 1) : 0;
      g_req.parse = Parse::HEADER;
      break;
    }
    case Parse::HEADER:
      if (g_req.lineLen == 0) {
        g_req.result.headersUs = (int64_t)time_us_64();
        if (g_req.chunked) {
          g_req.parse = Parse::CHUNK_SIZE;
        } else if (g_req.contentLength >= 0) {
          g_req.remain = (size_t)g_req.contentLength;
          g_req.parse = g_req.remain ? Parse::BODY_LEN : Parse::DONE;
        } else {
          g_req.parse = Parse::BODY_CLOSE;  // 長さが無ければ切断まで
          g_req.close = true;
        }
      } else if (startsWithNoCase(l, "content-length:")) {
        g_req.contentLength = atol(l + 15);
      } else if (startsWithNoCase(l, "transfer-encoding:")) {
        g_req.chunked = containsNoCase(l + 18, "chunked");
      } else if (startsWithNoCase(l, "connection:")) {
        g_req.close = containsNoCase(l + 11, "close");
      }
      break;
    case Parse::CHUNK_SIZE: {
      const unsigned long n = strtoul(l, nullptr, 16);
      g_req.remain = (size_t)n;
      g_req.parse = n ? Parse::CHUNK_DATA : Parse::TRAILER;
      break;
    }
    case Parse::CHUNK_END:
      g_req.parse = Parse::CHUNK_SIZE;
      break;
    case Parse::TRAILER:
      if (g_req.lineLen == 0) g_req.parse = Parse::DONE;
      break;
    default:
      break;
  }
}

// 受け取ったバイト列を解釈する。本文が入りきらなければ false
bool feed(const uint8_t *data, size_t n) {
  size_t i = 0;
  while (i < n && g_req.parse != Parse::DONE) {
    switch (g_req.parse) {
      case Parse::BODY_LEN:
      case Parse::CHUNK_DATA:
      case Parse::BODY_CLOSE: {
        size_t take = n - i;
        if (g_req.parse != Parse::BODY_CLOSE && take > g_req.remain) take = g_req.remain;
        if (g_req.len + take > g_req.cap) return false;
        memcpy(g_req.buf + g_req.len, data + i, take);
        g_req.len += take;
        i += take;
        if (g_req.parse != Parse::BODY_CLOSE) {
          g_req.remain -= take;
          if (g_req.remain == 0) g_req.parse = g_req.parse == Parse::BODY_LEN ? Parse::DONE : Parse::CHUNK_END;
        }
        break;
      }
      default: {
        const char c = (char)data[i++];
        if (c == '\n') {
          if (g_req.lineLen > 0 && g_req.line[g_req.lineLen - 1] == '\r') g_req.lineLen--;
          g_req.line[g_req.lineLen] = 0;
          handleLine();
          g_req.lineLen = 0;
        } else if (g_req.lineLen + 1 < sizeof g_req.line) {
          g_req.line[g_req.lineLen++] = c;
        }
        break;
      }
    }
  }
  return true;
}

bool sendRequest(struct altcp_pcb *pcb) {
  char req[320];
  const int n = snprintf(req, sizeof req,
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: TinyKamiokande-MCU\r\n"
                         "Accept: application/cbor\r\nConnection: keep-alive\r\n\r\n",
                         g_req.path, g_req.host);
  if (n <= 0 || n >= (int)sizeof req) return false;
  if (altcp_write(pcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
  if (altcp_output(pcb) != ERR_OK) return false;
  g_req.result.sentUs = (int64_t)time_us_64();
  return true;
}

// 使い回した接続が要求の途中で切れたら、1 度だけ新しい接続で送り直す
void failOrRetry() {
  if (g_req.result.reused && !g_req.gotBytes && !g_req.retried) {
    g_req.retryPending = true;
  } else {
    finish(false);
  }
}

err_t onRecv(void *, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
  if (!p || err != ERR_OK) {
    // サーバが閉じた
    if (p) pbuf_free(p);
    const bool aborted = closePcb(false);
    if (g_req.state == HttpState::BUSY) {
      if (g_req.parse == Parse::BODY_CLOSE) finish(g_req.status == 200);
      else failOrRetry();
    }
    return aborted ? ERR_ABRT : ERR_OK;
  }
  const u16_t total = p->tot_len;
  bool ok = true;
  if (g_req.state == HttpState::BUSY) {
    g_req.gotBytes = true;
    for (struct pbuf *q = p; q && ok; q = q->next) ok = feed((const uint8_t *)q->payload, q->len);
  }
  altcp_recved(pcb, total);
  pbuf_free(p);
  if (g_req.state != HttpState::BUSY) return ERR_OK;
  if (!ok) {
    std::printf("http: response larger than %u bytes\n", (unsigned)g_req.cap);
    finish(false);
    closePcb(true);
    return ERR_ABRT;
  }
  if (g_req.parse == Parse::DONE) {
    finish(g_req.status == 200);
    if (g_req.close && closePcb(false)) return ERR_ABRT;
  }
  return ERR_OK;
}

void onErr(void *, err_t err) {
  // pcb は lwIP が解放済み
  g_pcb = nullptr;
  g_pcbReady = false;
  std::printf("http: connection error %d\n", (int)err);
  if (g_req.state == HttpState::BUSY) failOrRetry();
}

err_t onConnected(void *, struct altcp_pcb *pcb, err_t err) {
  if (err != ERR_OK) {
    std::printf("http: connect failed %d\n", (int)err);
    finish(false);
    return closePcb(true) ? ERR_ABRT : ERR_OK;
  }
  // TLS の握手が済んだ。次に繋ぎ直すときのためにセッション (チケット) を取っておく
  g_pcbReady = true;
  g_req.result.handshakeUs = (uint32_t)(time_us_64() - g_req.connectStartUs);
  if (g_haveSession) altcp_tls_free_session(&g_session);
  altcp_tls_init_session(&g_session);
  g_haveSession = altcp_tls_get_session(pcb, &g_session) == ERR_OK;
  if (!sendRequest(pcb)) {
    finish(false);
    return closePcb(true) ? ERR_ABRT : ERR_OK;
  }
  return ERR_OK;
}

void connectTo(const ip_addr_t *addr) {
  if (!g_pcb) return;
  if (altcp_connect(g_pcb, addr, HTTPS_PORT, onConnected) != ERR_OK) {
    std::printf("http: connect not started\n");
    closePcb(true);
    finish(false);
  }
}

void onDns(const char *name, const ip_addr_t *addr, void *) {
  if (g_req.state != HttpState::BUSY) return;
  if (!addr) {
    std::printf("http: cannot resolve %s\n", name);
    closePcb(true);
    finish(false);
    return;
  }
  connectTo(addr);
}

// 新しい接続を作って要求を送る (lwIP のロックの中で)
void startConnection() {
  closePcb(true);
  g_req.result.reused = false;
  g_req.connectStartUs = time_us_64();
  struct altcp_pcb *pcb = altcp_tls_new(g_tls, IPADDR_TYPE_V4);
  if (!pcb) {
    std::printf("http: no memory for a connection\n");
    finish(false);
    return;
  }
  g_pcb = pcb;
  altcp_arg(pcb, nullptr);
  altcp_recv(pcb, onRecv);
  altcp_err(pcb, onErr);
  mbedtls_ssl_set_hostname((mbedtls_ssl_context *)altcp_tls_context(pcb), g_req.host);
  if (g_haveSession) altcp_tls_set_session(pcb, &g_session);
  ip_addr_t addr;
  const err_t e = dns_gethostbyname(g_req.host, &addr, onDns, nullptr);
  if (e == ERR_OK) {
    connectTo(&addr);
  } else if (e != ERR_INPROGRESS) {
    std::printf("http: dns error %d\n", (int)e);
    closePcb(true);
    finish(false);
  }
}

// ---------------------------------------------------------------- WiFi (lwIP のロックの中で)

bool parseIp(const char *s, ip4_addr_t &out) { return s && s[0] && ip4addr_aton(s, &out); }

void startJoin(uint64_t nowUs) {
  closePcb(true);
  struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
  if (g_joinIssued) cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
  ip4_addr_t ip, mask, gw, dns;
  if (parseIp(g_cfg.ip, ip)) {
    // 静的 IP: DHCP を止めてアドレスを入れておく (つながった時点で使える)
    dhcp_release_and_stop(n);
    if (!parseIp(g_cfg.mask, mask)) IP4_ADDR(&mask, 255, 255, 255, 0);
    if (!parseIp(g_cfg.gateway, gw)) ip4_addr_set_zero(&gw);
    netif_set_addr(n, &ip, &mask, &gw);
    if (parseIp(g_cfg.dns, dns) || parseIp(g_cfg.gateway, dns)) {
      ip_addr_t d;
      ip_addr_copy_from_ip4(d, dns);
      dns_setserver(0, &d);
    }
  } else {
    ip4_addr_t zero;
    ip4_addr_set_zero(&zero);
    netif_set_addr(n, &zero, &zero, &zero);
    dhcp_start(n);
  }
  const uint32_t auth = g_cfg.pass[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN;
  const int err = cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass[0] ? g_cfg.pass : nullptr, auth);
  if (err) std::printf("wifi: join not started (%d)\n", err);
  g_joinIssued = true;
  g_attemptUs = nowUs;
  g_retryAtUs = 0;
}

}  // namespace

bool netInitOnThisCore() {
  if (cyw43_arch_init() != 0) {
    std::printf("wifi: CYW43 init failed\n");
    return false;
  }
  cyw43_arch_enable_sta_mode();
  // 省電力の待ちで応答が遅れないように
  cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
  {
    Lock lock;
    g_tls = altcp_tls_create_config_client((const u8_t *)TKRP2_ROOT_CERTS, sizeof TKRP2_ROOT_CERTS);
  }
  if (!g_tls) {
    std::printf("http: TLS config not created (root certificates)\n");
    return false;
  }
  g_ready = true;
  return true;
}

void netSetConfig(const tkapp::WifiConfig &cfg) {
  Lock lock;
  g_cfg = cfg;
  g_haveCfg = cfg.ssid[0] != 0;
  if (g_haveCfg) {
    std::printf("wifi: connecting to '%s' (%s)\n", cfg.ssid, cfg.ip[0] ? cfg.ip : "DHCP");
    g_connectPending = true;
  } else if (g_ready) {
    closePcb(true);
    if (g_joinIssued) cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    g_joinIssued = false;
    g_ip[0] = 0;
  }
}

void netPoll(uint64_t nowUs) {
  if (!g_ready) {
    g_state = tkapp::WifiState::UNAVAILABLE;
    return;
  }
  if (nowUs - g_lastPollUs < POLL_INTERVAL_US && !g_connectPending) return;
  g_lastPollUs = nowUs;
  Lock lock;
  if (!g_haveCfg) {
    g_state = tkapp::WifiState::NO_CONFIG;
    return;
  }
  if (g_connectPending) {
    g_connectPending = false;
    g_downSinceUs = nowUs;
    g_state = tkapp::WifiState::CONNECTING;
    startJoin(nowUs);
    return;
  }
  if (g_retryAtUs && nowUs >= g_retryAtUs) startJoin(nowUs);
  const int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  if (st == CYW43_LINK_UP) {
    if (g_state != tkapp::WifiState::CONNECTED) {
      snprintf(g_ip, sizeof g_ip, "%s", ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));
      std::printf("wifi: connected, ip %s\n", g_ip);
      g_state = tkapp::WifiState::CONNECTED;
    }
    return;
  }
  if (g_state == tkapp::WifiState::CONNECTED) {
    std::printf("wifi: disconnected (status %d)\n", st);
    g_downSinceUs = nowUs;
    g_ip[0] = 0;
    g_retryAtUs = nowUs + RECONNECT_DELAY_US;
  } else if (g_retryAtUs == 0) {
    if (st == CYW43_LINK_FAIL || st == CYW43_LINK_NONET || st == CYW43_LINK_BADAUTH) {
      std::printf("wifi: join failed (status %d)\n", st);
      g_retryAtUs = nowUs + RECONNECT_DELAY_US;
    } else if (nowUs - g_attemptUs > JOIN_TIMEOUT_US) {
      std::printf("wifi: join timed out (status %d)\n", st);
      g_retryAtUs = nowUs;
    }
  }
  g_state = nowUs - g_downSinceUs > FAILED_AFTER_US ? tkapp::WifiState::FAILED : tkapp::WifiState::CONNECTING;
}

tkapp::WifiState netState() { return g_state; }

const char *netIpString() { return g_state == tkapp::WifiState::CONNECTED ? g_ip : ""; }

bool httpStart(const char *url, uint8_t *buf, size_t cap, uint64_t nowUs) {
  if (!g_ready) return false;
  Lock lock;
  if (g_req.state == HttpState::BUSY) return false;
  if (!parseUrl(url, g_req.host, sizeof g_req.host, g_req.path, sizeof g_req.path)) {
    std::printf("http: bad url %s\n", url);
    return false;
  }
  g_req.buf = buf;
  g_req.cap = cap;
  g_req.result = HttpResult();
  resetParse();
  g_req.retried = g_req.retryPending = false;
  g_req.startUs = nowUs;
  g_req.state = HttpState::BUSY;
  if (g_pcb && g_pcbReady) {
    g_req.result.reused = true;
    if (!sendRequest(g_pcb)) {
      g_req.retried = true;
      startConnection();
    }
  } else {
    startConnection();
  }
  return true;
}

HttpState httpPoll(uint64_t nowUs) {
  Lock lock;
  if (g_req.state == HttpState::BUSY) {
    if (g_req.retryPending) {
      g_req.retryPending = false;
      g_req.retried = true;
      resetParse();
      startConnection();
    } else if (nowUs - g_req.startUs > HTTP_TIMEOUT_US) {
      std::printf("http: timed out\n");
      closePcb(true);
      finish(false);
    }
  }
  return g_req.state;
}

const HttpResult &httpResult() { return g_req.result; }

void httpFinish() {
  Lock lock;
  if (g_req.state != HttpState::BUSY) g_req.state = HttpState::IDLE;
}

}  // namespace tkrp2
