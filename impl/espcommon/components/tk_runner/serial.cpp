#include "serial.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#else
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#endif

#include "net.hpp"
#include "pipeline.hpp"
#include "tkapp/config.hpp"

namespace tkrunner {

namespace {

constexpr uint32_t STACK_BYTES = 6 * 1024;

struct ExtraCommand {
  const char *name, *help;
  void (*fn)();
};
ExtraCommand g_extra[4];
volatile int g_histCmd = -3;  // serialTakeHistoryCommand()
int g_extraCount = 0;

// 1 行読む (改行を除く)。エコーする。EOF なら false
bool readLine(char *buf, size_t cap) {
  size_t n = 0;
  static int prev = 0;  // CRLF の LF を読み飛ばすため (VFS が CR を LF に変えることもある)
  for (;;) {
    const int c = std::fgetc(stdin);
    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    const int p = prev;
    prev = c;
    if (c == '\n' && p == '\r') continue;
    if (c == '\r' || c == '\n') {
      std::printf("\n");
      break;
    }
    if (c == 8 || c == 127) {
      if (n > 0) {
        n--;
        std::printf("\b \b");
      }
      continue;
    }
    if (n + 1 < cap && c >= 32 && c < 127) {
      buf[n++] = (char)c;
      std::putchar(c);
    }
    std::fflush(stdout);
  }
  buf[n] = 0;
  return true;
}

void ask(const char *prompt, char *dst, size_t cap, const char *current) {
  if (current && current[0]) std::printf("%s [%s]: ", prompt, current);
  else std::printf("%s: ", prompt);
  std::fflush(stdout);
  char line[128];
  readLine(line, sizeof line);
  if (line[0] == 0 && current) return;  // 空なら今の値のまま
  if (!strcmp(line, "-")) {             // '-' で消す
    dst[0] = 0;
    return;
  }
  const size_t n = strnlen(line, cap - 1);
  memcpy(dst, line, n);
  dst[n] = 0;
}

void cmdWifi() {
  tkapp::WifiConfig cfg;
  netLoadConfig(cfg);
  std::printf("WiFi settings (Enter keeps the current value, '-' clears it)\n");
  ask("SSID", cfg.ssid, sizeof cfg.ssid, cfg.ssid);
  ask("Passphrase", cfg.pass, sizeof cfg.pass, cfg.pass[0] ? "(set)" : nullptr);
  ask("IPv4 address (blank: DHCP)", cfg.ip, sizeof cfg.ip, cfg.ip);
  if (cfg.ip[0]) {
    ask("Subnet mask", cfg.mask, sizeof cfg.mask, cfg.mask);
    ask("Default gateway", cfg.gateway, sizeof cfg.gateway, cfg.gateway);
    ask("DNS server", cfg.dns, sizeof cfg.dns, cfg.dns);
  } else {
    cfg.mask[0] = cfg.gateway[0] = cfg.dns[0] = 0;
  }
  if (cfg.ssid[0] == 0) {
    std::printf("SSID is empty; nothing saved\n");
    return;
  }
  netApplyConfig(cfg);
  std::printf("saved. connecting...\n");
}

void cmdStatus() {
  static const char *names[] = {"no config", "connecting", "connected", "failed", "unavailable"};
  const tkapp::WifiState st = netState();
  tkapp::WifiConfig cfg;
  const bool have = netLoadConfig(cfg);
  std::printf("wifi: %s%s%s", names[(int)st], have ? ", ssid " : "", have ? cfg.ssid : "");
  if (netIpString()[0]) std::printf(", ip %s", netIpString());
  std::printf("\n");
  const PipelineStatus &ps = pipelineStatus();
  std::printf("pipeline: fetch %lu, ok %lu, unchanged %lu, few lit %lu, errors %lu, last http %d, last fetch %lu ms, "
              "last fit %lu ms\n",
              (unsigned long)ps.fetches, (unsigned long)ps.ok, (unsigned long)ps.unchanged, (unsigned long)ps.fewLit,
              (unsigned long)ps.errors, ps.lastHttpStatus, (unsigned long)ps.lastFetchMs, (unsigned long)ps.lastFitMs);
}

void serialTask(void *) {
  char line[64];
  for (;;) {
    std::printf("> ");
    std::fflush(stdout);
    readLine(line, sizeof line);
    if (!strcmp(line, "wifi")) cmdWifi();
    else if (!strcmp(line, "status")) cmdStatus();
    else if (!strcmp(line, "hist")) g_histCmd = -1;
    else if (!strncmp(line, "hist ", 5)) g_histCmd = atoi(line + 5);
    else if (!strcmp(line, "live")) g_histCmd = -2;
    else if (!strcmp(line, "clear")) {
      netClearConfig();
      std::printf("credentials cleared\n");
    } else if (!strcmp(line, "help") || !strcmp(line, "?")) {
      std::printf("commands: wifi (set credentials), status, clear (erase credentials), hist [N] (list / select history), "
                  "live, help");
      for (int i = 0; i < g_extraCount; i++) std::printf(", %s (%s)", g_extra[i].name, g_extra[i].help);
      std::printf("\n");
    } else if (line[0]) {
      bool found = false;
      for (int i = 0; i < g_extraCount && !found; i++) {
        if (!strcmp(line, g_extra[i].name)) {
          found = true;
          g_extra[i].fn();
        }
      }
      if (!found) std::printf("unknown command '%s' (try 'help')\n", line);
    }
  }
}

}  // namespace

int serialTakeHistoryCommand() {
  const int c = g_histCmd;
  g_histCmd = -3;
  return c;
}

void serialAddCommand(const char *name, const char *help, void (*fn)()) {
  if (g_extraCount < (int)(sizeof(g_extra) / sizeof(g_extra[0]))) g_extra[g_extraCount++] = {name, help, fn};
}

void serialStart(int core) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) usb_serial_jtag_vfs_use_driver();
#else
  uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, nullptr, 0);
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
  std::setvbuf(stdin, nullptr, _IONBF, 0);
  xTaskCreatePinnedToCore(serialTask, "tk_serial", STACK_BYTES, nullptr, 1, nullptr, core);
}

}  // namespace tkrunner

extern "C" void tkSerialAddCommand(const char *name, const char *help, void (*fn)(void)) {
  tkrunner::serialAddCommand(name, help, fn);
}
