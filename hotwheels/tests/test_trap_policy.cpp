// tests/test_trap_policy.cpp — SPEC-048 (HW-069): ¿el runtime se detiene en el
// primer import sin shim?
//
// El host NUNCA se detiene (el juego sigue con stubs que devuelven 0) y así
// llega a DirectSoundCreate, o sea, hay audio. En Vita el default era
// detenerse: sin env.txt el juego paraba en KERNEL32!GetVersion y se quedaba
// sin audio. La parada queda como diagnóstico opt-in.
#include "runtime/trap_policy.h"

#include <cassert>
#include <cstdio>

int main() {
    using hw::runtime::stop_at_first_trap;
    assert(!stop_at_first_trap(nullptr));   // sin env: no se detiene (igual que el host)
    assert(!stop_at_first_trap("0"));       // explícito: no
    assert(stop_at_first_trap("1"));        // opt-in del diagnóstico
    assert(stop_at_first_trap("si"));       // cualquier valor distinto de 0
    std::puts("TRAP-POLICY: PASS");
}
