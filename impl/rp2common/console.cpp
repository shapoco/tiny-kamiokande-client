#include "console.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net.hpp"
#include "pico/stdlib.h"
#include "pipeline.hpp"

namespace tkrp2 {

namespace {

ConsoleHooks g_hooks = {};
char g_line[128];
size_t g_len = 0;
int g_prev = 0;  // CRLF の LF を読み飛ばすため

// wifi コマンドの対話の段階。-1 なら対話していない
enum Step { STEP_NONE = -1, STEP_SSID, STEP_PASS, STEP_IP, STEP_MASK, STEP_GATEWAY, STEP_DNS };
int g_step = STEP_NONE;
tkapp::WifiConfig g_draft;

struct Field {
  const char *prompt;
  char *dst;
  size_t cap;
};

Field field(int step) {
  switch (step) {
    case STEP_SSID: return {"SSID", g_draft.ssid, sizeof g_draft.ssid};
    case STEP_PASS: return {"Passphrase", g_draft.pass, sizeof g_draft.pass};
    case STEP_IP: return {"IPv4 address (blank: DHCP)", g_draft.ip, sizeof g_draft.ip};
    case STEP_MASK: return {"Subnet mask", g_draft.mask, sizeof g_draft.mask};
    case STEP_GATEWAY: return {"Default gateway", g_draft.gateway, sizeof g_draft.gateway};
    default: return {"DNS server", g_draft.dns, sizeof g_draft.dns};
  }
}

void prompt() {
  if (g_step == STEP_NONE) {
    std::printf("> ");
  } else {
    const Field f = field(g_step);
    const char *cur = g_step == STEP_PASS ? (f.dst[0] ? "(set)" : "") : f.dst;
    if (cur[0]) std::printf("%s [%s]: ", f.prompt, cur);
    else std::printf("%s: ", f.prompt);
  }
  std::fflush(stdout);
}

void cmdStatus() {
  static const char *names[] = {"no config", "connecting", "connected", "failed", "unavailable"};
  const tkapp::WifiConfig &cfg = g_hooks.current();
  std::printf("wifi: %s%s%s", names[(int)netState()], cfg.ssid[0] ? ", ssid " : "", cfg.ssid);
  if (netIpString()[0]) std::printf(", ip %s", netIpString());
  std::printf("\n");
  const PipelineStatus &ps = pipelineStatus();
  std::printf("pipeline: fetch %lu, ok %lu, unchanged %lu, few lit %lu, errors %lu, dropped %lu, last http %d, "
              "last fetch %lu ms (%lu bytes), connections new %lu reused %lu, last handshake %lu ms\n",
              (unsigned long)ps.fetches, (unsigned long)ps.ok, (unsigned long)ps.unchanged, (unsigned long)ps.fewLit,
              (unsigned long)ps.errors, (unsigned long)ps.dropped, ps.lastHttpStatus, (unsigned long)ps.lastFetchMs,
              (unsigned long)ps.lastBytes, (unsigned long)ps.newConnections, (unsigned long)ps.reused,
              (unsigned long)ps.lastHandshakeMs);
  if (g_hooks.status) g_hooks.status();
}

// wifi の対話の 1 行
void answer(const char *line) {
  const Field f = field(g_step);
  if (!strcmp(line, "-")) {
    f.dst[0] = 0;
  } else if (line[0]) {
    const size_t n = strnlen(line, f.cap - 1);
    memcpy(f.dst, line, n);
    f.dst[n] = 0;
  }
  if (g_step == STEP_IP && !g_draft.ip[0]) {
    g_draft.mask[0] = g_draft.gateway[0] = g_draft.dns[0] = 0;
    g_step = STEP_DNS;  // DHCP なら残りは聞かない
  }
  if (g_step < STEP_DNS) {
    g_step++;
    return;
  }
  g_step = STEP_NONE;
  if (g_draft.ssid[0] == 0) {
    std::printf("SSID is empty; nothing saved\n");
    return;
  }
  g_hooks.apply(g_draft);
  std::printf("saved. connecting...\n");
}

void command(const char *line) {
  if (!strcmp(line, "wifi")) {
    g_draft = g_hooks.current();
    std::printf("WiFi settings (Enter keeps the current value, '-' clears it)\n");
    g_step = STEP_SSID;
  } else if (!strcmp(line, "status")) {
    cmdStatus();
  } else if (!strcmp(line, "clear")) {
    g_hooks.apply(tkapp::WifiConfig());
    std::printf("credentials cleared\n");
  } else if (!strcmp(line, "hist") || !strncmp(line, "hist ", 5)) {
    g_hooks.history(line[4] ? atoi(line + 5) : -1);
  } else if (!strcmp(line, "live")) {
    g_hooks.live();
  } else if (!strcmp(line, "help") || !strcmp(line, "?")) {
    std::printf("commands: wifi (set credentials), status, clear (erase credentials), hist [N] (list / select history), "
                "live, help\n");
  } else if (line[0]) {
    std::printf("unknown command '%s' (try 'help')\n", line);
  }
}

}  // namespace

void consoleStart(const ConsoleHooks &hooks) { g_hooks = hooks; }

void consolePoll() {
  for (;;) {
    const int c = getchar_timeout_us(0);
    if (c < 0) return;
    const int p = g_prev;
    g_prev = c;
    if (c == '\n' && p == '\r') continue;
    if (c == '\r' || c == '\n') {
      std::printf("\n");
      g_line[g_len] = 0;
      g_len = 0;
      if (g_step == STEP_NONE) command(g_line);
      else answer(g_line);
      prompt();
      continue;
    }
    if (c == 8 || c == 127) {
      if (g_len > 0) {
        g_len--;
        std::printf("\b \b");
        std::fflush(stdout);
      }
      continue;
    }
    if (g_len + 1 < sizeof g_line && c >= 32 && c < 127) {
      g_line[g_len++] = (char)c;
      std::putchar(c);
      std::fflush(stdout);
    }
  }
}

}  // namespace tkrp2
