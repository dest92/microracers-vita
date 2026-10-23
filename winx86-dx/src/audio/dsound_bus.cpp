// Bounded producer/consumer ring for DirectSound PCM.
#include "../../include/winx86-dx/audio/dsound_bus.h"

#include <algorithm>
#include <cstddef>

namespace winx86::dx::audio {

namespace {
int nonnegative(int value) { return value < 0 ? 0 : value; }
}

DsoundBus::DsoundBus(const DsoundBusConfig& config, ClockFn clock)
    : ring_(static_cast<size_t>(nonnegative(config.capacity_frames)) * 2, 0),
      capacity_frames_(nonnegative(config.capacity_frames)),
      prime_ms_(nonnegative(config.prime_ms)),
      prime_max_ms_(nonnegative(config.prime_max_ms)),
      prime_growth_ms_(nonnegative(config.prime_growth_ms)),
      clock_(clock ? clock : &DsoundBus::steady_now) {}

DsoundBus::DsoundBus(int capacity_frames, ClockFn clock)
    : DsoundBus(DsoundBusConfig{capacity_frames, 120, 400, 40}, clock) {}

void DsoundBus::push(const int16_t* pcm, int frames, int rate) {
    if (!pcm || frames <= 0 || capacity_frames_ <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const int old_rate = rate_;
    const bool was_empty = size_ == 0;
    rate_ = rate;
    if (frames > capacity_frames_) {
        pcm += static_cast<size_t>(frames - capacity_frames_) * 2;
        overruns_ += static_cast<uint64_t>(frames - capacity_frames_);
        frames = capacity_frames_;
    }
    const int space = capacity_frames_ - size_;
    if (frames > space) {
        const int drop = frames - space;
        overruns_ += static_cast<uint64_t>(drop);
        head_ = (head_ + drop) % capacity_frames_;
        size_ -= drop;
        if (size_ < 0) size_ = 0;
    }
    int tail = (head_ + size_) % capacity_frames_;
    for (int frame = 0; frame < frames; ++frame) {
        ring_[static_cast<size_t>(tail) * 2] = pcm[static_cast<size_t>(frame) * 2];
        ring_[static_cast<size_t>(tail) * 2 + 1] = pcm[static_cast<size_t>(frame) * 2 + 1];
        tail = (tail + 1) % capacity_frames_;
    }
    size_ += frames;
    if (!primed_ && (was_empty || old_rate != rate_ || !prime_timer_active_)) {
        prime_started_ = clock_();
        prime_timer_active_ = true;
    }
}

int DsoundBus::read(int16_t* out, int frames) {
    if (!out || frames <= 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!primed_ && prime_ms_ > 0) {
        if (size_ <= 0) {
            prime_timer_active_ = false;
            return 0;
        }
        const int64_t target = static_cast<int64_t>(rate_) * prime_ms_ / 1000;
        if (!prime_timer_active_) {
            prime_started_ = clock_();
            prime_timer_active_ = true;
        }
        const auto elapsed = clock_() - prime_started_;
        if (size_ < target && elapsed < std::chrono::milliseconds(prime_ms_)) return 0;
    }
    primed_ = true;
    prime_timer_active_ = false;
    const int count = std::min(frames, size_);
    for (int frame = 0; frame < count; ++frame) {
        out[static_cast<size_t>(frame) * 2] = ring_[static_cast<size_t>(head_) * 2];
        out[static_cast<size_t>(frame) * 2 + 1] = ring_[static_cast<size_t>(head_) * 2 + 1];
        head_ = (head_ + 1) % capacity_frames_;
    }
    size_ -= count;
    if (count > 0) readable_space_.notify_all();
    if (count < frames) {
        ++underruns_;
        primed_ = false;
        if (prime_ms_ > 0 && prime_ms_ < prime_max_ms_)
            prime_ms_ = std::min(prime_max_ms_, prime_ms_ + prime_growth_ms_);
    }
    if (size_ == 0) {
        primed_ = false;
        prime_timer_active_ = false;
    } else if (count < frames) {
        prime_started_ = clock_();
        prime_timer_active_ = true;
    }
    return count;
}

int DsoundBus::rate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rate_;
}

uint64_t DsoundBus::underruns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return underruns_;
}

uint64_t DsoundBus::overruns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return overruns_;
}

void DsoundBus::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    size_ = 0;
    primed_ = false;
    prime_timer_active_ = false;
}

void DsoundBus::set_prime_ms(int ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    prime_ms_ = nonnegative(ms);
    prime_timer_active_ = size_ > 0 && !primed_;
    if (prime_timer_active_) prime_started_ = clock_();
}

int DsoundBus::prime_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return prime_ms_;
}

int DsoundBus::level_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rate_ > 0 ? static_cast<int>(static_cast<int64_t>(size_) * 1000 / rate_) : 0;
}

bool DsoundBus::wait_below(int target_ms, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return readable_space_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        return rate_ <= 0 || static_cast<int64_t>(size_) * 1000 / rate_ <= target_ms;
    });
}

}  // namespace winx86::dx::audio
