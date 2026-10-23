#include "runtime/pmu_metrics.h"

#include <cassert>
#include <cstdint>

int main() {
    using d2rt::pmu::Counters;
    using d2rt::pmu::delta;
    using d2rt::pmu::ipc_milli;

    const Counters before{0xfffffff0u, 0xfffffff8u, 90u, 0xfffffff0u};
    const Counters after {0x00000010u, 0x00000018u, 100u, 0x00000008u};
    const Counters d = delta(before, after);
    assert(d.cycles == 32u);
    assert(d.inst == 32u);
    assert(d.br_mispred == 10u);
    assert(d.dc_miss == 24u);

    uint64_t ipc = 0;
    assert(ipc_milli(2000u, 1500u, &ipc));
    assert(ipc == 750u);
    assert(!ipc_milli(0u, 0u, &ipc));
    return 0;
}
