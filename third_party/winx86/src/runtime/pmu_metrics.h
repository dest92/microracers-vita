#pragma once
#include <cstdint>

// Implemented by dyn86.c; used by the native scheduler on host and Vita.
namespace d2rt {
#ifdef __vita__
extern "C" void dyn86_pmu_dispatch_reset(void);
extern "C" uint64_t dyn86_pmu_dispatch_read(void);
extern "C" uint32_t dyn86_pmu_dispatch_samples_read(void);
#else
// The native scheduler shares this code with host builds, where the ARM PMU
// instrumentation does not exist. Compile-time no-ops preserve that ABI.
inline void dyn86_pmu_dispatch_reset(void) {}
inline uint64_t dyn86_pmu_dispatch_read(void) { return 0; }
inline uint32_t dyn86_pmu_dispatch_samples_read(void) { return 0; }
#endif

namespace pmu {
struct Counters { uint32_t cycles = 0, inst = 0, br_mispred = 0, dc_miss = 0; };

// One modular subtraction is exact if fewer than 2^32 events occurred.
inline Counters delta(const Counters& before, const Counters& after) {
    return Counters{after.cycles - before.cycles, after.inst - before.inst,
                    after.br_mispred - before.br_mispred, after.dc_miss - before.dc_miss};
}
inline bool ipc_milli(uint64_t cycles, uint64_t inst, uint64_t* out) {
    if (!out || !cycles) return false;
    *out = inst * 1000ull / cycles;
    return true;
}
} // namespace pmu
} // namespace d2rt
