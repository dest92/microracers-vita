#pragma once
#include "diag/thread_cpu.h"
namespace hw::clock_probe {
inline constexpr uint32_t kWaiting = 8;
struct Sample {
    hw::diag::ThreadSample thread;
    uint32_t wait_type = 0;
    int wait_id = 0, cpu_mhz = 0;
};
inline bool integrity(const Sample& a, const Sample& b, int barrier) {
    return barrier > 0 && a.thread.generation > 0 &&
        hw::diag::thread_delta(a.thread, b.thread).status == hw::diag::DeltaStatus::Valid &&
        (a.thread.state & kWaiting) && (b.thread.state & kWaiting) &&
        a.wait_type != 0 && a.wait_type == b.wait_type &&
        a.wait_id == barrier && b.wait_id == barrier;
}
inline bool target_verified(unsigned core, int pin_rc, const Sample& a, const Sample& b) {
    if (core > 2 || pin_rc != 0 || a.thread.rc != 0 || b.thread.rc != 0) return false;
    const int mask = int(0x10000u << core);
    return a.thread.mask == mask && b.thread.mask == mask &&
           a.thread.last_cpu == int(core) && b.thread.last_cpu == int(core);
}
inline bool config_stable(const Sample& a, const Sample& b) {
    return a.thread.rc == 0 && b.thread.rc == 0 && a.cpu_mhz > 0 && a.cpu_mhz == b.cpu_mhz && a.thread.priority == b.thread.priority;
}
} // namespace hw::clock_probe
