#include "runtime/guest_eip_profile.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#ifdef __vita__
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr/thread.h>
#endif

extern "C" {
// HW-051 is an additive winx86 patch. Keep the base runtime linkable when a
// consumer (for example the dxlib integration build) uses an unpatched winx86.
// The profiler remains functional when the patch supplies these definitions.
void dyn86_guestprof_enable(int on) __attribute__((weak));
void dyn86_guestprof_set_state(uint32_t state) __attribute__((weak));
uint32_t dyn86_guestprof_read_state(void) __attribute__((weak));
uint32_t dyn86_guestprof_read_eip(void) __attribute__((weak));
}

namespace hw::runtime {
namespace {
constexpr auto kSamplePeriod = std::chrono::microseconds(1000);

bool truthy(const char* value) {
    return value && value[0] && value[0] != '0' && value[0] != 'n' && value[0] != 'N';
}
uint32_t env_u32(const char* key, uint32_t fallback) {
    const char* value = std::getenv(key);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    return end && !*end && parsed <= UINT32_MAX ? static_cast<uint32_t>(parsed) : fallback;
}
std::string scene_name(const char* value) {
    if (value && (std::string(value) == "menu" || std::string(value) == "race")) return value;
    return "unknown";
}
void load_symbols(const char* path, std::vector<GuestProfileSymbolRange>& symbols) {
    if (!path || !*path) return;
    std::ifstream in(path);
    std::string line;
    std::vector<GuestProfileSymbolRange> raw;
    std::vector<uint32_t> boundaries;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string start, end, entry, name;
        if (!std::getline(row, start, ',') || !std::getline(row, end, ',') ||
            !std::getline(row, entry, ',') || !std::getline(row, name) || name.empty()) continue;
        char* a_end = nullptr;
        char* b_end = nullptr;
        char* e_end = nullptr;
        const unsigned long a = std::strtoul(start.c_str(), &a_end, 0);
        const unsigned long b = std::strtoul(end.c_str(), &b_end, 0);
        const unsigned long e = std::strtoul(entry.c_str(), &e_end, 0);
        if (!a_end || *a_end || !b_end || *b_end || !e_end || *e_end ||
            a >= b || b > UINT32_MAX || e > UINT32_MAX) continue;
        raw.push_back({static_cast<uint32_t>(a), static_cast<uint32_t>(b),
                       static_cast<uint32_t>(e), name});
        boundaries.push_back(static_cast<uint32_t>(a));
        boundaries.push_back(static_cast<uint32_t>(b));
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    for (size_t i = 1; i < boundaries.size(); ++i) {
        const uint32_t a = boundaries[i - 1], b = boundaries[i];
        const GuestProfileSymbolRange* best = nullptr;
        for (const auto& candidate : raw) {
            if (candidate.start <= a && candidate.end >= b &&
                (!best || candidate.end - candidate.start < best->end - best->start)) best = &candidate;
        }
        if (!best) continue;
        if (!symbols.empty() && symbols.back().end == a && symbols.back().entry == best->entry &&
            symbols.back().name == best->name) symbols.back().end = b;
        else symbols.push_back({a, b, best->entry, best->name});
    }
}

uint32_t function_key(uint32_t eip, const std::vector<GuestProfileSymbolRange>& symbols) {
    auto it = std::upper_bound(symbols.begin(), symbols.end(), eip,
        [](uint32_t ip, const GuestProfileSymbolRange& row) { return ip < row.start; });
    if (it != symbols.begin()) {
        --it;
        if (eip >= it->start && eip < it->end) return it->entry;
    }
    return eip & ~0xFFFu; // stable fallback bucket when no Ghidra range exists
}
} // namespace

GuestEipProfile::~GuestEipProfile() { if (armed_) (void)stop(); }

bool GuestEipProfile::start_from_env() {
    if (armed_ || !truthy(std::getenv("HW_GUEST_PROFILE"))) return false;
    if (!dyn86_guestprof_enable || !dyn86_guestprof_set_state ||
        !dyn86_guestprof_read_state || !dyn86_guestprof_read_eip) return false;
    scene_ = scene_name(std::getenv("HW_GUEST_PROFILE_SCENE"));
    window_start_ms_ = env_u32("HW_GUEST_PROFILE_START_MS", 0);
    duration_ms_ = env_u32("HW_GUEST_PROFILE_DURATION_MS", 0);
    load_symbols(std::getenv("HW_GUEST_PROFILE_MAP"), symbols_);
    stopping_.store(false, std::memory_order_relaxed);
    armed_ = true;
    dyn86_guestprof_enable(1);
    try {
        thread_ = std::thread(&GuestEipProfile::sample_loop, this);
    } catch (...) {
        dyn86_guestprof_enable(0);
        armed_ = false;
        return false;
    }
    return true;
}

void GuestEipProfile::sample_loop() {
#ifdef __vita__
    vita_affinity_attempted_ = true;
    vita_affinity_rc_ = sceKernelChangeThreadCpuAffinityMask(
        sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_1);
    vita_priority_rc_ = sceKernelChangeThreadPriority(sceKernelGetThreadId(), 128);
#endif
    using clock = std::chrono::steady_clock;
    const auto origin = clock::now();
    const auto begin = origin + std::chrono::milliseconds(window_start_ms_);
    while (!stopping_.load(std::memory_order_relaxed) && clock::now() < begin) {
        const auto wake = std::min(begin, clock::now() + kSamplePeriod);
        std::this_thread::sleep_until(wake);
    }
    if (stopping_.load(std::memory_order_relaxed)) return;
    const auto end = duration_ms_ ? begin + std::chrono::milliseconds(duration_ms_) : clock::time_point::max();
    auto next = begin;
    while (!stopping_.load(std::memory_order_relaxed) && clock::now() < end) {
        const uint32_t state = dyn86_guestprof_read_state();
        if (state == 1) {
            const uint32_t eip = dyn86_guestprof_read_eip();
            std::lock_guard<std::mutex> lock(result_mutex_);
            ++total_; ++guest_;
            if (eip) ++counts_[function_key(eip, symbols_)];
        } else if (state == 2) {
            std::lock_guard<std::mutex> lock(result_mutex_);
            ++total_; ++shim_;
        } else {
            std::lock_guard<std::mutex> lock(result_mutex_);
            ++idle_;
        }
        next += kSamplePeriod;
        std::this_thread::sleep_until(next);
    }
}

GuestProfileResult GuestEipProfile::stop() {
    GuestProfileResult out;
    if (!armed_) return out;
    stopping_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
    dyn86_guestprof_set_state(0);
    dyn86_guestprof_enable(0);
    std::lock_guard<std::mutex> lock(result_mutex_);
    out.scene = scene_; out.window_start_ms = window_start_ms_; out.duration_ms = duration_ms_;
    out.total = total_; out.guest = guest_; out.shim = shim_; out.idle = idle_;
    std::set<uint32_t> mapped_entries;
    for (const auto& symbol : symbols_) mapped_entries.insert(symbol.entry);
    out.mapped_functions = mapped_entries.size();
    out.vita_affinity_attempted = vita_affinity_attempted_;
    out.vita_affinity_rc = vita_affinity_rc_;
    out.vita_priority_rc = vita_priority_rc_;
    for (const auto& [key, samples] : counts_) {
        GuestProfileRow row;
        bool found = false;
        for (const auto& symbol : symbols_) {
            if (symbol.entry == key) {
                if (!found) { row.start = symbol.entry; row.name = symbol.name; found = true; }
                row.end = std::max(row.end, symbol.end);
            }
        }
        if (!found) { row.start = key; row.end = key + 0x1000u; row.name = "unknown_"; }
        if (row.name == "unknown_") {
            char buf[24]; std::snprintf(buf, sizeof(buf), "sub_%08X", key); row.name = buf;
        }
        row.samples = samples;
        out.top.push_back(std::move(row));
    }
    std::sort(out.top.begin(), out.top.end(), [](const auto& a, const auto& b) {
        return a.samples != b.samples ? a.samples > b.samples : a.start < b.start;
    });
    if (out.top.size() > 30) out.top.resize(30);
    armed_ = false;
    return out;
}

} // namespace hw::runtime
