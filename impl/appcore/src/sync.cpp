#include "tkapp/sync.hpp"

#include "tkapp/config.hpp"

namespace tkapp {

void DisplaySync::noteServerTime(int64_t serverTimeMs, int64_t sentUs, int64_t headersUs) {
  if (serverTimeMs <= 0 || headersUs <= 0) return;
  samples_[next_] = {headersUs - sentUs, serverTimeMs - headersUs / 1000};
  next_ = (next_ + 1) % SAMPLES;
  if (count_ < SAMPLES) count_++;
  int best = 0;
  for (int i = 1; i < count_; i++)
    if (samples_[i].waitUs < samples_[best].waitUs) best = i;
  offsetMs_ = samples_[best].offsetMs;
  valid_ = true;
}

int64_t DisplaySync::delayMs(int64_t nowUs) const {
  if (rampStartUs_ < 0) return 0;
  float ramp = (float)(nowUs - rampStartUs_) / (QUEUE_RAMP_SEC * 1e6f);
  if (ramp > 1) ramp = 1;
  if (ramp < 0) ramp = 0;
  return (int64_t)(QUEUE_DELAY_SEC * 1000.0f * ramp);
}

bool DisplaySync::isDue(int64_t timestampMs, int64_t nowUs) const {
  if (timestampMs <= 0 || !valid_ || rampStartUs_ < 0) return true;
  const int64_t waitMs = timestampMs + delayMs(nowUs) - (nowUs / 1000 + offsetMs_);
  return waitMs <= 0 || waitMs > (int64_t)(QUEUE_DELAY_SEC * 2000.0f);
}

}  // namespace tkapp
