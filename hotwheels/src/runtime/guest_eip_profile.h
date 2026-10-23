#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hw::runtime {

struct GuestProfileRow {
    uint32_t start = 0; // function entry
    uint32_t end = 0;   // highest exclusive mapped function range
    std::string name;
    uint64_t samples = 0;
};

struct GuestProfileSymbolRange {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t entry = 0;
    std::string name;
};

struct GuestProfileResult {
    std::string scene;
    uint32_t window_start_ms = 0;
    uint32_t duration_ms = 0;
    uint64_t total = 0;
    uint64_t guest = 0;
    uint64_t shim = 0;
    uint64_t idle = 0;
    size_t mapped_functions = 0;
    bool vita_affinity_attempted = false;
    int vita_affinity_rc = 0;
    int vita_priority_rc = 0;
    std::vector<GuestProfileRow> top;
};

class GuestEipProfile {
public:
    GuestEipProfile() = default;
    ~GuestEipProfile();
    GuestEipProfile(const GuestEipProfile&) = delete;
    GuestEipProfile& operator=(const GuestEipProfile&) = delete;

    // Starts only when HW_GUEST_PROFILE is truthy. Returns whether it armed.
    bool start_from_env();
    GuestProfileResult stop();

private:
    void sample_loop();
    bool armed_ = false;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    std::string scene_ = "unknown";
    uint32_t window_start_ms_ = 0;
    uint32_t duration_ms_ = 0;
    std::vector<GuestProfileSymbolRange> symbols_;
    std::map<uint32_t, uint64_t> counts_;
    uint64_t total_ = 0, guest_ = 0, shim_ = 0, idle_ = 0;
    bool vita_affinity_attempted_ = false;
    int vita_affinity_rc_ = 0, vita_priority_rc_ = 0;
    std::mutex result_mutex_;
};

} // namespace hw::runtime
