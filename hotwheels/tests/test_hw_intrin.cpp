// HW-052: parseo de HW_INTRIN (sólo 0/1/2; cualquier otra cosa apaga).
#include "runtime/hw_intrin.h"
#include <cassert>
#include <cstdlib>
int main() {
    using hw::intrin::parse_mode;
    assert(parse_mode(nullptr) == 0 && parse_mode("") == 0);
    assert(parse_mode("1") == 1 && parse_mode("2") == 2 && parse_mode("0") == 0);
    assert(parse_mode("3") == 0 && parse_mode("12") == 0 && parse_mode("x") == 0);
    setenv("HW_HLE_RASTER", "1", 1); unsetenv("HW_INTRIN");
    assert(hw::intrin::mode_from_env() == 1);
    setenv("HW_INTRIN", "0", 1);  // HW_INTRIN manda sobre el alias
    assert(hw::intrin::mode_from_env() == 0);
    unsetenv("HW_INTRIN"); unsetenv("HW_HLE_RASTER");
    assert(hw::intrin::mode_from_env() == 0);
    unsigned n = 99; const auto* t = hw::intrin::targets(&n);
    assert(n == 5 && t[4].va == 0x4521c4 && t[2].va == 0x41d5a0 && t[3].va == 0x420d30 && t[0].va == 0x450af3 && t[1].va == 0x450c19 && t[0].retn == 0 && t[1].inmask == 0);
    using hw::intrin::span_ft3_in_bounds;
    const uint32_t span = 0x0c400000;
    assert(span_ft3_in_bounds(0x500100, 64, 0x01000000, span));
    assert(!span_ft3_in_bounds(0x500100, 64, 0x0c3f0000 + 0x10000, span));  // página fuera
    assert(!span_ft3_in_bounds(0x20, 64, 0x01000000, span));                 // dst-n < 0
    assert(!span_ft3_in_bounds(span + 1, 1, 0x01000000, span));
    assert(!span_ft3_in_bounds(0x500100, 5000, 0x01000000, span));
    return 0;
}
