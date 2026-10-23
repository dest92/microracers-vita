// Thread-safe bounded ring for guest-produced DirectSound PCM.
#pragma once

#include "pcm.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace winx86::dx::audio {

struct DsoundBusConfig {
    int capacity_frames = kSampleRate * 4;
    int prime_ms = 120;
    int prime_max_ms = 400;
    int prime_growth_ms = 40;
};

class DsoundBus : public PcmSource {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using ClockFn = TimePoint (*)();

    // push/read/wait_below may run on separate producer/consumer threads;
    // mutex/condition-variable synchronization protects the ring and counters.
    // The optional clock callback runs under the ring mutex and must not reenter.
    explicit DsoundBus(const DsoundBusConfig& config = {}, ClockFn clock = nullptr);
    DsoundBus(int capacity_frames, ClockFn clock = nullptr);
    DsoundBus(const DsoundBus&) = delete;
    DsoundBus& operator=(const DsoundBus&) = delete;
    DsoundBus(DsoundBus&&) = delete;
    DsoundBus& operator=(DsoundBus&&) = delete;

    // push() is producer-side and never waits for free ring capacity; old
    // frames are dropped and counted when a burst exceeds the configured ring.
    void push(const int16_t* pcm, int frames, int rate);
    int read(int16_t* out, int frames) override;
    int rate() const override;
    uint64_t underruns() const;
    uint64_t overruns() const;
    void reset();
    void set_prime_ms(int ms);
    int prime_ms() const;
    bool wait_below(int target_ms, int timeout_ms);
    int level_ms() const;

private:
    static TimePoint steady_now() { return std::chrono::steady_clock::now(); }

    mutable std::mutex mutex_;
    std::condition_variable readable_space_;
    std::vector<int16_t> ring_;
    int capacity_frames_;
    int head_ = 0;
    int size_ = 0;
    int rate_ = kSampleRate;
    uint64_t underruns_ = 0, overruns_ = 0;
    int prime_ms_;
    int prime_max_ms_;
    int prime_growth_ms_;
    TimePoint prime_started_{};
    bool prime_timer_active_ = false;
    bool primed_ = false;
    ClockFn clock_;
};

}  // namespace winx86::dx::audio
