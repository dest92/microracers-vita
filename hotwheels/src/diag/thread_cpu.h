#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#endif

namespace hw::diag {
enum class ThreadRole : unsigned { Guest, Render, Dsound, Feeder, Music };
inline constexpr unsigned kThreadRoles = 5;
inline constexpr const char* kThreadNames[kThreadRoles] = {
    "guest-main", "render", "dsound", "feeder", "music"
};

// Un token indivisible evita mezclar el TID de una vida con la generación de otra.
class ThreadRegistry {
    std::array<std::atomic<uint64_t>, kThreadRoles> slots_{};
public:
    void set(unsigned role, int tid) {
        uint64_t old = slots_[role].load(std::memory_order_relaxed);
        uint64_t next;
        do {
            next = ((old + (uint64_t(1) << 32)) & 0xffffffff00000000ull) | uint32_t(tid);
        } while (!slots_[role].compare_exchange_weak(old, next,
                     std::memory_order_release, std::memory_order_relaxed));
    }
    uint64_t token(unsigned role) const { return slots_[role].load(std::memory_order_acquire); }
    int get(unsigned role) const { return int(uint32_t(token(role))); }
};
inline ThreadRegistry thread_registry;

class ThreadRegistration {
    unsigned role_;
public:
    explicit ThreadRegistration(ThreadRole role) : role_(unsigned(role)) {
#ifdef __vita__
        thread_registry.set(role_, sceKernelGetThreadId());
#endif
    }
    ~ThreadRegistration() {
#ifdef __vita__
        thread_registry.set(role_, 0);
#endif
    }
    ThreadRegistration(const ThreadRegistration&) = delete;
    ThreadRegistration& operator=(const ThreadRegistration&) = delete;
};

struct ThreadSample {
    int tid = 0, rc = 0;
    uint32_t generation = 0;
    uint64_t wall_us = 0, clocks = 0;
    int mask = 0, cpu = -1, last_cpu = -1, priority = 0;
    uint32_t state = 0;
};
enum class DeltaStatus { Valid, Missing, ApiError, ChangedTid, ChangedGeneration, TimeError, CounterReset };
struct ThreadDelta { DeltaStatus status; uint64_t clocks; };
inline ThreadDelta thread_delta(const ThreadSample& a, const ThreadSample& b) {
    if (a.tid <= 0 || b.tid <= 0) return {DeltaStatus::Missing, 0};
    if (a.rc != 0 || b.rc != 0) return {DeltaStatus::ApiError, 0};
    if (a.tid != b.tid) return {DeltaStatus::ChangedTid, 0};
    if (a.generation != b.generation) return {DeltaStatus::ChangedGeneration, 0};
    if (b.wall_us <= a.wall_us) return {DeltaStatus::TimeError, 0};
    if (b.clocks < a.clocks) return {DeltaStatus::CounterReset, 0};
    return {DeltaStatus::Valid, b.clocks - a.clocks};
}
inline const char* delta_status_name(DeltaStatus s) {
    switch (s) {
    case DeltaStatus::Valid: return "valid";
    case DeltaStatus::Missing: return "missing";
    case DeltaStatus::ApiError: return "api_error";
    case DeltaStatus::ChangedTid: return "changed_tid";
    case DeltaStatus::ChangedGeneration: return "changed_generation";
    case DeltaStatus::TimeError: return "time_error";
    case DeltaStatus::CounterReset: return "counter_reset";
    }
    return "invalid";
}
inline void format_thread_window(char* out, size_t cap, const char* role,
                                 const char* declared, uint32_t width, uint32_t height,
                                 uint32_t frames_begin, uint32_t frames_end,
                                 const ThreadSample& a, const ThreadSample& b) {
    const auto d = thread_delta(a, b);
    char raw[32];
    if (d.status == DeltaStatus::Valid)
        std::snprintf(raw, sizeof raw, "%llu", (unsigned long long)d.clocks);
    else std::snprintf(raw, sizeof raw, "unavailable");
    char size[32];
    if (width && height) std::snprintf(size, sizeof size, "%ux%u", width, height);
    else std::snprintf(size, sizeof size, "unknown");
    std::snprintf(out, cap,
        "[hw] thread-cpu role=%s scene=unverified scene_declared=%.80s size=%s "
        "frames=%u:%u tid=%d:%d generation=%u:%u rc=%d:%d "
        "t_us=%llu:%llu elapsed_us=%llu runclocks=%llu:%llu "
        "delta_status=%s delta_valid=%d delta_raw=%s "
        "mask=0x%x:0x%x cpu=%d:%d last_cpu=%d:%d prio=%d:%d state=0x%x:0x%x "
        "affinity_changed=%d priority_changed=%d",
        role, declared ? declared : "unknown", size, frames_begin, frames_end,
        a.tid, b.tid, a.generation, b.generation, a.rc, b.rc,
        (unsigned long long)a.wall_us, (unsigned long long)b.wall_us,
        (unsigned long long)(b.wall_us > a.wall_us ? b.wall_us - a.wall_us : 0),
        (unsigned long long)a.clocks, (unsigned long long)b.clocks,
        delta_status_name(d.status), d.status == DeltaStatus::Valid, raw,
        unsigned(a.mask), unsigned(b.mask), a.cpu, b.cpu, a.last_cpu, b.last_cpu,
        a.priority, b.priority, a.state, b.state,
        a.mask != b.mask, a.priority != b.priority);
}
#ifdef __vita__
inline ThreadSample sample_thread(unsigned role) {
    ThreadSample s;
    const auto token = thread_registry.token(role);
    s.tid = int(uint32_t(token)); s.generation = uint32_t(token >> 32);
    SceKernelThreadInfo info{}; info.size = sizeof info;
    s.rc = s.tid > 0 ? sceKernelGetThreadInfo(s.tid, &info) : -1;
    s.wall_us = sceKernelGetProcessTimeWide();
    // Cambio durante la syscall: no atribuir la muestra a la vida anterior.
    if (thread_registry.token(role) != token) s.rc = -1;
    if (s.rc == 0) {
        s.clocks = info.runClocks; s.mask = info.currentCpuAffinityMask;
        s.cpu = info.currentCpuId; s.last_cpu = info.lastExecutedCpuId;
        s.priority = info.currentPriority; s.state = info.status;
    }
    return s;
}
inline std::array<ThreadSample, kThreadRoles> sample_threads() {
    std::array<ThreadSample, kThreadRoles> samples;
    for (unsigned i = 0; i < kThreadRoles; ++i) samples[i] = sample_thread(i);
    return samples;
}
#endif
} // namespace hw::diag
