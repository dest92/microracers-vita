#pragma once

#include <cstdint>

namespace hw::integration {

// Guest-side audio telemetry is rate-limited to keep durable memory-card I/O
// out of the per-frame path. Caller supplies a monotonic millisecond clock.
class AudioMetricsGate {
public:
    explicit AudioMetricsGate(uint64_t period_ms = 10000) : period_ms_(period_ms) {}

    bool due(uint64_t now_ms) {
        if (!started_ || now_ms - last_ms_ >= period_ms_) {
            started_ = true;
            last_ms_ = now_ms;
            return true;
        }
        return false;
    }

private:
    const uint64_t period_ms_;
    uint64_t last_ms_ = 0;
    bool started_ = false;
};

}  // namespace hw::integration
