// ESP32-C6 (WiFi コプロセッサ) のファームをホストから更新する。
// Tab5 出荷時の C6 は esp_hosted 1.x 相当で、ホスト 3.x と組み合わせると 1536 バイトを超える受信フレームが捨てられ
// TLS が通らない。esp_hosted のコプロセッサ用ファーム (cp_firmware/tab5_c6_hosted.bin。SPEC.md 5.6) を
// ビルド時に埋め込み、シリアルの 'c6ota' コマンドで OTA RPC (begin → write → end → activate) で書き込む。
// esp_hosted のヘッダは C++ から読めないので C で書く
#include <esp_hosted.h>
#include <esp_hosted_ota.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdio.h>

#include "cp_ota.hpp"
extern void tkSerialAddCommand(const char *name, const char *help, void (*fn)(void));


extern const uint8_t g_cpImageStart[] asm("_binary_tab5_c6_hosted_bin_start");
extern const uint8_t g_cpImageEnd[] asm("_binary_tab5_c6_hosted_bin_end");

#define CHUNK 1024u

static void cmdC6Ota(void) {
  const size_t total = (size_t)(g_cpImageEnd - g_cpImageStart);
  if (total < 4096) {
    printf("c6ota: no image embedded (%u bytes)\n", (unsigned)total);
    return;
  }
  esp_hosted_coprocessor_fwver_t ver = {};
  if (esp_hosted_get_coprocessor_fwversion(&ver) == 0) {
    printf("c6ota: coprocessor firmware %u.%u.%u\n", (unsigned)ver.major1, (unsigned)ver.minor1, (unsigned)ver.patch1);
  }
  printf("c6ota: writing %u bytes to the ESP32-C6...\n", (unsigned)total);
  esp_err_t err = esp_hosted_cp_ota_begin();
  if (err != ESP_OK) {
    printf("c6ota: begin failed (%d)\n", (int)err);
    return;
  }
  const int64_t t0 = esp_timer_get_time();
  size_t done = 0;
  int lastPct = -1;
  while (done < total) {
    const uint32_t n = total - done < CHUNK ? (uint32_t)(total - done) : CHUNK;
    err = esp_hosted_cp_ota_write(g_cpImageStart + done, n);
    if (err != ESP_OK) {
      printf("c6ota: write failed at %u (%d)\n", (unsigned)done, (int)err);
      return;
    }
    done += n;
    const int pct = (int)(done * 100 / total);
    if (pct / 10 != lastPct / 10) {
      printf("c6ota: %d%%\n", pct);
      lastPct = pct;
    }
  }
  err = esp_hosted_cp_ota_end();
  if (err != ESP_OK) {
    printf("c6ota: end failed (%d)\n", (int)err);
    return;
  }
  printf("c6ota: written in %lld ms, activating...\n", (long long)((esp_timer_get_time() - t0) / 1000));
  err = esp_hosted_cp_ota_activate();
  printf("c6ota: activate -> %d (older firmware reboots by itself after end). reset the Tab5 to use the new firmware\n", (int)err);
}

void cpOtaRegister(void) { tkSerialAddCommand("c6ota", "update the ESP32-C6 firmware", cmdC6Ota); }
