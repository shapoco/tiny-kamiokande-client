#include "nvs_store.hpp"

#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>


namespace tkrunner {

namespace {

constexpr const char *NVS_NAMESPACE = "tinykamiokande";

bool g_inited = false, g_ok = false;

bool ensureInit() {
  if (g_inited) return g_ok;
  g_inited = true;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    std::printf("nvs: erasing (%s)\n", esp_err_to_name(err));
    if (nvs_flash_erase() == ESP_OK) err = nvs_flash_init();
  }
  g_ok = err == ESP_OK;
  if (!g_ok) std::printf("nvs: init failed (%s)\n", esp_err_to_name(err));
  return g_ok;
}

}  // namespace

bool loadBlob(const char *key, void *data, size_t bytes) {
  if (!ensureInit()) return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = bytes;
  const esp_err_t err = nvs_get_blob(h, key, data, &len);
  nvs_close(h);
  return err == ESP_OK && len == bytes;
}

bool saveBlob(const char *key, const void *data, size_t bytes) {
  if (!ensureInit()) return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(h, key, data, bytes);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) std::printf("nvs: write of %s failed (%s)\n", key, esp_err_to_name(err));
  return err == ESP_OK;
}

}  // namespace tkrunner
