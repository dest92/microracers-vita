// Compatibility unit: shared VFS/base shims plus Hot Wheels clock metrics formatting.
#include "shims/shims.h"
#include "../../../winx86-dx/src/io/vfs.cpp"
#include "../../../winx86-dx/src/shims/base.cpp"
#include "../../../winx86-dx/src/shims/imports.cpp"

namespace hw::shims {

bool GuestClock::take_stats_line(std::string& out) {
    winx86::dx::shims::GuestClock::Stats stats;
    if (!take_stats(stats)) return false;
    const uint64_t dt_ms = stats.window_us / 1000;
    out = "qpc: window_ms=" + std::to_string(dt_ms) + " qpc_calls=" + std::to_string(stats.qpc_calls) +
          " idles=" + std::to_string(stats.idles) + " calls_per_s=" + std::to_string(stats.calls_per_second);
    return true;
}

} // namespace hw::shims
