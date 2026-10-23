#include "runtime/guest_dt_profile.h"

#include <chrono>
#include <utility>

namespace hw::runtime {

uint32_t GuestDtResult::percentile(double p) const {
    if (waits == 0 || histogram.empty()) return 0;
    if (p <= 0.0) return histogram.begin()->first;
    if (p >= 1.0) return histogram.rbegin()->first;
    // Rango objetivo (ceil) sobre el total de esperas.
    uint64_t target = uint64_t(p * double(waits));
    if (target == 0) target = 1;
    uint64_t seen = 0;
    for (const auto& row : histogram) {
        seen += row.second;
        if (seen >= target) return row.first;
    }
    return histogram.rbegin()->first;
}

void GuestDtSampler::sample() {
    const uint64_t prev = read_prev_ ? read_prev_() : 0;
    const uint32_t dt = read_dt_ ? read_dt_() : 0;
    ++result_.samples;
    if (!have_prev_) {
        last_prev_ = prev;
        have_prev_ = true;
        return;
    }
    if (prev == last_prev_) return;
    last_prev_ = prev;
    // Terminó una espera: el dt leído es el que el invitado usó.
    ++result_.waits;
    result_.sum_dt += dt;
    if (result_.waits == 1) {
        result_.min_dt = dt;
        result_.max_dt = dt;
    } else {
        if (dt < result_.min_dt) result_.min_dt = dt;
        if (dt > result_.max_dt) result_.max_dt = dt;
    }
    if (dt < kGuestDtMin || dt > kGuestDtMax) ++result_.out_of_range;
    ++result_.histogram[dt];
    result_.distinct = uint32_t(result_.histogram.size());
}

bool GuestDtProfile::start() {
    if (running_) return false;
    stopping_.store(false, std::memory_order_relaxed);
    try {
        thread_ = std::thread(&GuestDtProfile::loop, this);
    } catch (...) {
        return false;
    }
    running_ = true;
    return true;
}

GuestDtResult GuestDtProfile::stop() {
    if (running_) {
        stopping_.store(true, std::memory_order_relaxed);
        if (thread_.joinable()) thread_.join();
        running_ = false;
    }
    return sampler_.result();
}

void GuestDtProfile::loop() {
    while (!stopping_.load(std::memory_order_relaxed)) {
        sampler_.sample();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace hw::runtime
