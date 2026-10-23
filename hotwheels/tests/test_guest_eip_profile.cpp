#include "runtime/guest_eip_profile.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <string>

namespace {
std::atomic<uint32_t> state{0};
std::atomic<uint32_t> eip{0};
}
#ifndef HW_TEST_NO_DYN86_PROFILE
extern "C" void dyn86_guestprof_enable(int) {}
extern "C" void dyn86_guestprof_set_state(uint32_t s) { state.store(s); }
extern "C" uint32_t dyn86_guestprof_read_state(void) { return state.load(); }
extern "C" uint32_t dyn86_guestprof_read_eip(void) { return eip.load(); }
#endif

int main(int argc, char** argv) {
#ifdef HW_TEST_NO_DYN86_PROFILE
    (void)argc;
    (void)argv;
    setenv("HW_GUEST_PROFILE", "1", 1);
    hw::runtime::GuestEipProfile profile;
    assert(!profile.start_from_env());
    const auto result = profile.stop();
    assert(result.total == 0 && result.guest == 0 && result.shim == 0 && result.idle == 0);
    return 0;
#else
    assert(argc == 2);
    setenv("HW_GUEST_PROFILE", "1", 1);
    setenv("HW_GUEST_PROFILE_SCENE", "menu", 1);
    setenv("HW_GUEST_PROFILE_DURATION_MS", "0", 1);
    setenv("HW_GUEST_PROFILE_MAP", argv[1], 1);
    hw::runtime::GuestEipProfile profile;
    assert(profile.start_from_env());
    eip.store(0x00401003); state.store(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    eip.store(0x00402004); state.store(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    eip.store(0x0040300a); state.store(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    state.store(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    state.store(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    const auto result = profile.stop();
    assert(result.scene == "menu");
    assert(result.mapped_functions == 4);
    assert(result.guest > 10 && result.shim > 0 && result.idle > 0);
    assert(result.top.size() == 3);
    bool saw_a = false, saw_b = false, saw_inner = false, saw_outer = false;
    for (const auto& row : result.top) {
        saw_a |= row.name == "func_A" && row.samples > 0;
        saw_b |= row.name == "func_B" && row.samples > 0;
        saw_inner |= row.name == "inner" && row.samples > 0;
        saw_outer |= row.name == "outer" && row.samples > 0;
    }
    assert(saw_a && saw_b && saw_inner && !saw_outer);
    return 0;
#endif
}
