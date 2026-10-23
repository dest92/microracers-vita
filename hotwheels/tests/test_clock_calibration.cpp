#include "measurement.h"
#include <cassert>
#include <cstdio>
using namespace hw::clock_probe;
int main() {
    Sample a{}, b{};
    a.thread.tid = b.thread.tid = 17;
    a.thread.generation = b.thread.generation = 1;
    a.thread.state = b.thread.state = kWaiting;
    a.wait_type = b.wait_type = 32; a.wait_id = b.wait_id = 42;
    a.cpu_mhz = b.cpu_mhz = 444;
    a.thread.wall_us = 100; b.thread.wall_us = 200100;
    b.thread.clocks = uint64_t(1) << 40;
    for (unsigned core = 0; core < 3; ++core) {
        a.thread.mask = b.thread.mask = int(0x10000u << core);
        a.thread.last_cpu = b.thread.last_cpu = int(core);
        assert(integrity(a, b, 42));
        assert(target_verified(core, 0, a, b));
        assert(config_stable(a, b));
        assert(!target_verified(core, -1, a, b));
    }
    a.thread.mask = b.thread.mask = 0x80000;
    a.thread.last_cpu = b.thread.last_cpu = 3;
    assert(!target_verified(3, 0, a, b));
    a.thread.mask = b.thread.mask = 0x40000;
    a.thread.last_cpu = b.thread.last_cpu = 2;
    assert(!target_verified(4, 0, a, b));
    assert(!target_verified(UINT32_MAX, 0, a, b));
    assert(!target_verified(2, 1, a, b));
    b.thread.mask = 0; assert(!target_verified(2, 0, a, b)); b.thread.mask = a.thread.mask;
    b.thread.last_cpu = 0; assert(!target_verified(2, 0, a, b)); b.thread.last_cpu = 2;
    a.thread.mask = 0;
    assert(integrity(a, b, 42)); assert(!target_verified(2, 0, a, b));
    a.thread.mask = b.thread.mask;
    a.thread.last_cpu = 0; assert(!target_verified(2, 0, a, b)); a.thread.last_cpu = 2;
    a.cpu_mhz = b.cpu_mhz = 0; assert(!config_stable(a, b));
    a.cpu_mhz = b.cpu_mhz = -1; assert(!config_stable(a, b));
    a.cpu_mhz = b.cpu_mhz = 444;
    a.thread.rc = -1; assert(!integrity(a, b, 42));
    assert(!target_verified(2, 0, a, b)); assert(!config_stable(a, b)); a.thread.rc = 0;
    b.cpu_mhz = 333; assert(!config_stable(a, b)); b.cpu_mhz = 444;
    b.thread.priority = 128; assert(!config_stable(a, b)); b.thread.priority = 0;
    b.thread.rc = -1; assert(!integrity(a, b, 42));
    assert(!target_verified(2, 0, a, b)); assert(!config_stable(a, b)); b.thread.rc = 0;
    a.thread.tid = 0; assert(!integrity(a, b, 42)); a.thread.tid = 17;
    b.thread.tid = 18; assert(!integrity(a, b, 42)); b.thread.tid = 17;
    a.thread.generation = b.thread.generation = 0; assert(!integrity(a, b, 42));
    a.thread.generation = b.thread.generation = 1;
    b.thread.generation = 2; assert(!integrity(a, b, 42)); b.thread.generation = 1;
    b.thread.wall_us = 100; assert(!integrity(a, b, 42)); b.thread.wall_us = 200100;
    a.thread.clocks = 2; b.thread.clocks = 1; assert(!integrity(a, b, 42));
    a.thread.clocks = b.thread.clocks = 0; assert(integrity(a, b, 42));
    a.thread.state = 1; assert(!integrity(a, b, 42)); a.thread.state = kWaiting;
    b.thread.state = 1; assert(!integrity(a, b, 42)); b.thread.state = kWaiting;
    a.wait_id = b.wait_id = 0; assert(!integrity(a, b, 0)); a.wait_id = b.wait_id = 42;
    a.wait_type = b.wait_type = 0; assert(!integrity(a, b, 42)); a.wait_type = b.wait_type = 32;
    a.wait_type = 0; assert(!integrity(a, b, 42)); a.wait_type = 32;
    b.wait_type = 33; assert(!integrity(a, b, 42)); b.wait_type = 32;
    a.wait_id = 41; assert(!integrity(a, b, 42)); a.wait_id = 42;
    b.wait_id = 41; assert(!integrity(a, b, 42));
    std::puts("PASS clock_calibration: raw integrity, barrier identity, target and config separated");
}
