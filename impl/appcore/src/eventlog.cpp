#include "tkapp/eventlog.hpp"

namespace tkapp {

namespace {

uint32_t argb(int r, int g, int b) { return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }

// a から b へ t (0..1) の割合で補間
uint32_t mix(const int a[3], const int b[3], float t) {
  int c[3];
  for (int i = 0; i < 3; i++) c[i] = (int)(a[i] + (b[i] - a[i]) * t + 0.5f);
  return argb(c[0], c[1], c[2]);
}

// ARGB8888 → RGB565_SWAPPED
uint16_t toRgb565Swapped(uint32_t c) {
  const uint16_t v = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
  return (uint16_t)((v >> 8) | (v << 8));
}

uint16_t readU16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

}  // namespace

void EventLog::clear() {
  now_ = -1;
  for (int i = 0; i < CAPACITY; i++) slot_[i] = 0;
}

void EventLog::merge(int64_t nowSlot, const uint8_t *rows, size_t bytes) {
  if (nowSlot <= 0) return;
  now_ = nowSlot;
  for (size_t off = 0; off + LOG_ROW_BYTES <= bytes; off += LOG_ROW_BYTES) {
    const uint8_t *p = rows + off;
    const uint32_t slot = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    if (slot == 0 || (int64_t)slot > nowSlot || (int64_t)slot <= nowSlot - CAPACITY) continue;
    const int i = (int)(slot % CAPACITY);
    slot_[i] = slot;
    counts_[i].fetches = readU16(p + 4);
    for (int k = 0; k < LOG_KINDS; k++) counts_[i].n[k] = readU16(p + 6 + 2 * k);
  }
}

const LogCounts *EventLog::find(int64_t slot) const {
  if (slot <= 0 || now_ < 0 || slot > now_ || slot <= now_ - CAPACITY) return nullptr;
  const int i = (int)(slot % CAPACITY);
  return slot_[i] == (uint32_t)slot ? &counts_[i] : nullptr;
}

// 上の条件ほど優先 (index.html の logCellRgb と同じ)
uint32_t logCellColor(const LogCounts *c) {
  if (!c) return argb(0, 0, 0);  // 記録なし (誰もアクセスしなかった)
  const int err = c->n[LOG_ERROR], dim = c->n[LOG_DIM], unfit = c->n[LOG_UNFIT];
  const int through = c->n[LOG_THROUGH], stopping = c->n[LOG_STOPPING];
  const int inside = c->n[LOG_INSIDE] + c->n[LOG_CONTAINED];
  const int total = err + dim + unfit + through + stopping + inside;
  if (total == 0) return c->fetches > 0 ? argb(40, 40, 40) : argb(0, 0, 0);  // イベントなし
  if (err * 2 >= total) return argb(150, 50, 200);             // 半分以上がエラー
  if ((dim + unfit) * 2 >= total) return argb(120, 120, 120);  // 半分以上が推定不能・発光不足
  auto level = [](int n) { return ((n < 5 ? n : 5) - 1) / 4.0f; };
  if (inside > 0) {
    static const int a[3] = {255, 150, 0}, b[3] = {230, 20, 20};
    return mix(a, b, level(inside));
  }
  if (stopping > 0) {
    static const int a[3] = {30, 180, 40}, b[3] = {240, 230, 40};
    return mix(a, b, level(stopping));
  }
  if (through > 0) {
    static const int a[3] = {30, 60, 230}, b[3] = {60, 220, 255};
    float t = (through - 10) / 20.0f;

    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return mix(a, b, t);
  }
  return argb(120, 120, 120);
}

void buildLogImage(const EventLog &log, int cols, uint16_t *pixels) {
  const int64_t now = log.nowSlot();
  const int64_t hour0 = now < 0 ? 0 : now / LOG_SLOTS_PER_HOUR * LOG_SLOTS_PER_HOUR;  // 現在の時の最初の枠
  for (int c = 0; c < cols; c++) {
    const int64_t slot0 = hour0 - (int64_t)(cols - 1 - c) * LOG_SLOTS_PER_HOUR;
    for (int r = 0; r < LOG_SLOTS_PER_HOUR; r++) {
      const int64_t slot = slot0 + r;
      const uint32_t color = now < 0 || slot > now ? argb(0, 0, 0) : logCellColor(log.find(slot));
      pixels[r * cols + c] = toRgb565Swapped(color);
    }
  }
}

}  // namespace tkapp
