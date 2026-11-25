#include "net.hpp"

#ifndef TKAPP_WIFI
#define TKAPP_WIFI 1
#endif

#if TKAPP_WIFI

#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/ip4_addr.h>
#include <nvs_flash.h>

#include <cstdio>
#include <cstring>

#include "nvs_store.hpp"

namespace tkrunner {

namespace {

constexpr const char *WIFI_KEY = "wifi";
constexpr int64_t RECONNECT_DELAY_US = 3000000;
constexpr int64_t FAILED_AFTER_US = 30000000;  // 接続できないままこの時間で FAILED 表示
constexpr int HTTP_TIMEOUT_MS = 15000;

tkapp::WifiConfig g_cfg;
bool g_haveCfg = false;
bool g_started = false;     // esp_wifi_start() 済み
bool g_updated = false;     // netTakeUpdatedConfig() 用
esp_netif_t *g_netif = nullptr;
SemaphoreHandle_t g_lock = nullptr;
volatile tkapp::WifiState g_state = tkapp::WifiState::NO_CONFIG;
volatile bool g_connected = false;
int64_t g_connectStartUs = 0;
esp_timer_handle_t g_reconnectTimer = nullptr;
char g_ip[16] = "";

struct Lock {
  Lock() { if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { if (g_lock) xSemaphoreGive(g_lock); }
};

bool parseIp(const char *s, esp_ip4_addr_t &out) {
  if (!s || !s[0]) return false;
  ip4_addr_t a;
  if (!ip4addr_aton(s, &a)) return false;
  out.addr = a.addr;
  return true;
}

// 静的 IP か DHCP を netif に設定する
void applyIpConfig() {
  if (!g_netif) return;
  esp_ip4_addr_t ip, mask, gw, dns;
  if (parseIp(g_cfg.ip, ip)) {
    esp_netif_dhcpc_stop(g_netif);
    esp_netif_ip_info_t info = {};
    info.ip = ip;
    if (!parseIp(g_cfg.mask, mask)) mask.addr = esp_netif_htonl(0xFFFFFF00u);
    info.netmask = mask;
    if (parseIp(g_cfg.gateway, gw)) info.gw = gw;
    esp_netif_set_ip_info(g_netif, &info);
    if (parseIp(g_cfg.dns, dns) || (parseIp(g_cfg.gateway, dns))) {
      esp_netif_dns_info_t d = {};
      d.ip.type = ESP_IPADDR_TYPE_V4;
      d.ip.u_addr.ip4 = dns;
      esp_netif_set_dns_info(g_netif, ESP_NETIF_DNS_MAIN, &d);
    }
  } else {
    esp_netif_dhcpc_start(g_netif);
  }
}

void connectNow() {
  wifi_config_t wc = {};
  // ssid / password は終端なしの固定長でよい
  memcpy(wc.sta.ssid, g_cfg.ssid, strnlen(g_cfg.ssid, sizeof wc.sta.ssid));
  memcpy(wc.sta.password, g_cfg.pass, strnlen(g_cfg.pass, sizeof wc.sta.password));
  wc.sta.threshold.authmode = g_cfg.pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  wc.sta.pmf_cfg.capable = true;
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  applyIpConfig();
  g_connected = false;
  g_connectStartUs = esp_timer_get_time();
  g_state = tkapp::WifiState::CONNECTING;
  if (!g_started) {
    g_started = true;
    esp_wifi_start();  // STA_START で connect する
  } else {
    esp_wifi_connect();
  }
}

void onReconnectTimer(void *) {
  if (g_haveCfg && !g_connected) esp_wifi_connect();
}

void onWifiEvent(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT) {
    if (id == WIFI_EVENT_STA_START) {
      if (g_haveCfg) esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
      g_connected = false;
      g_ip[0] = 0;
      if (g_haveCfg) {
        const auto *e = (wifi_event_sta_disconnected_t *)data;
        std::printf("wifi: disconnected (reason %d)\n", e ? e->reason : -1);
        g_state = esp_timer_get_time() - g_connectStartUs > FAILED_AFTER_US ? tkapp::WifiState::FAILED
                                                                             : tkapp::WifiState::CONNECTING;
        esp_timer_stop(g_reconnectTimer);
        esp_timer_start_once(g_reconnectTimer, RECONNECT_DELAY_US);
      }
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto *e = (ip_event_got_ip_t *)data;
    snprintf(g_ip, sizeof g_ip, IPSTR, IP2STR(&e->ip_info.ip));
    g_connected = true;
    g_state = tkapp::WifiState::CONNECTED;
    std::printf("wifi: connected, ip %s\n", g_ip);
  }
}

}  // namespace

bool netLoadConfig(tkapp::WifiConfig &cfg) {
  tkapp::WifiConfig tmp;
  if (!loadBlob(WIFI_KEY, &tmp, sizeof tmp)) return false;
  tmp.ssid[tkapp::SSID_MAX] = 0;
  tmp.pass[tkapp::PASS_MAX] = 0;
  tmp.ip[tkapp::IPV4_MAX] = tmp.mask[tkapp::IPV4_MAX] = tmp.gateway[tkapp::IPV4_MAX] = tmp.dns[tkapp::IPV4_MAX] = 0;
  if (tmp.ssid[0] == 0) return false;
  cfg = tmp;
  return true;
}

void netStart() {
  if (g_lock) return;
  g_lock = xSemaphoreCreateMutex();
  // nvs_store が初期化していなければここで
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  esp_netif_init();
  esp_event_loop_create_default();
  g_netif = esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&init);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, nullptr);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_timer_create_args_t ta = {};
  ta.callback = onReconnectTimer;
  ta.name = "wifi_retry";
  esp_timer_create(&ta, &g_reconnectTimer);

  g_haveCfg = netLoadConfig(g_cfg);
  if (g_haveCfg) {
    std::printf("wifi: connecting to '%s' (%s)\n", g_cfg.ssid, g_cfg.ip[0] ? g_cfg.ip : "DHCP");
    connectNow();
  } else {
    g_state = tkapp::WifiState::NO_CONFIG;
    std::printf("wifi: no credentials. type 'wifi' on the serial console or tap the icon\n");
  }
}

void netApplyConfig(const tkapp::WifiConfig &cfg) {
  Lock lock;
  g_cfg = cfg;
  saveBlob(WIFI_KEY, &g_cfg, sizeof g_cfg);
  g_updated = true;
  if (!g_netif) return;  // netStart() 前 (起動時に拾う)
  if (g_cfg.ssid[0] == 0) {
    netClearConfig();
    return;
  }
  g_haveCfg = true;
  if (g_started && g_connected) esp_wifi_disconnect();
  std::printf("wifi: connecting to '%s' (%s)\n", g_cfg.ssid, g_cfg.ip[0] ? g_cfg.ip : "DHCP");
  connectNow();
}

void netClearConfig() {
  g_haveCfg = false;
  tkapp::WifiConfig empty;
  g_cfg = empty;
  saveBlob(WIFI_KEY, &g_cfg, sizeof g_cfg);
  g_updated = true;
  if (g_started) esp_wifi_disconnect();
  g_connected = false;
  g_ip[0] = 0;
  g_state = tkapp::WifiState::NO_CONFIG;
}

tkapp::WifiState netState() { return g_state; }

const char *netIpString() { return g_connected ? g_ip : ""; }

bool netTakeUpdatedConfig(tkapp::WifiConfig &cfg) {
  Lock lock;
  if (!g_updated) return false;
  g_updated = false;
  cfg = g_cfg;
  return true;
}

int netFetch(const char *url, uint8_t *buf, size_t cap, int *httpStatus, NetTiming *timing) {
  if (httpStatus) *httpStatus = 0;
  if (timing) *timing = NetTiming();
  if (!g_connected) return -1;
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;
  cfg.keep_alive_enable = false;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return -2;
  int result = -3;
  esp_err_t err = esp_http_client_open(c, 0);
  if (err == ESP_OK) {
    const int64_t sentUs = esp_timer_get_time();
    const int64_t len = esp_http_client_fetch_headers(c);
    if (timing) {
      timing->sentUs = sentUs;
      timing->headersUs = esp_timer_get_time();
    }
    const int status = esp_http_client_get_status_code(c);
    if (httpStatus) *httpStatus = status;
    if (status == 200 && len <= (int64_t)cap) {
      size_t total = 0;
      for (;;) {
        if (total >= cap) break;
        const int n = esp_http_client_read(c, (char *)buf + total, (int)(cap - total));
        if (n < 0) {
          total = 0;
          break;
        }
        if (n == 0) break;
        total += (size_t)n;
      }
      result = total > 0 ? (int)total : -4;
    } else {
      std::printf("http: status %d, length %lld\n", status, (long long)len);
      result = -5;
    }
    esp_http_client_close(c);
  } else {
    std::printf("http: open failed (%d)\n", (int)err);
  }
  esp_http_client_cleanup(c);
  return result;
}

}  // namespace tkrunner

#else  // TKAPP_WIFI == 0: 通信なし。認証情報の保存だけ

#include <cstdio>
#include <cstring>

#include "nvs_store.hpp"

namespace tkrunner {

namespace {
constexpr const char *WIFI_KEY = "wifi";
tkapp::WifiConfig g_cfg;
bool g_updated = false;
}  // namespace

bool netLoadConfig(tkapp::WifiConfig &cfg) {
  tkapp::WifiConfig tmp;
  if (!loadBlob(WIFI_KEY, &tmp, sizeof tmp)) return false;
  tmp.ssid[tkapp::SSID_MAX] = 0;
  if (tmp.ssid[0] == 0) return false;
  cfg = tmp;
  return true;
}
void netApplyConfig(const tkapp::WifiConfig &cfg) {
  g_cfg = cfg;
  saveBlob(WIFI_KEY, &g_cfg, sizeof g_cfg);
  g_updated = true;
}
void netClearConfig() {
  g_cfg = tkapp::WifiConfig();
  saveBlob(WIFI_KEY, &g_cfg, sizeof g_cfg);
  g_updated = true;
}
void netStart() { std::printf("wifi: not built in (TKAPP_WIFI=0)\n"); }
tkapp::WifiState netState() { return tkapp::WifiState::UNAVAILABLE; }
const char *netIpString() { return ""; }
bool netTakeUpdatedConfig(tkapp::WifiConfig &cfg) {
  if (!g_updated) return false;
  g_updated = false;
  cfg = g_cfg;
  return true;
}
int netFetch(const char *, uint8_t *, size_t, int *httpStatus, NetTiming *timing) {
  if (httpStatus) *httpStatus = 0;
  if (timing) *timing = NetTiming();
  return -1;
}

}  // namespace tkrunner

#endif
