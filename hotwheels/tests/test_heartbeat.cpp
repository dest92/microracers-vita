// Spec 023: latidos por hilo para el vigía interno (BUG-035).
#include "runtime/heartbeat.h"

#include <cassert>
#include <cstring>
#include <string>

using namespace hw::hb;

int main() {
    reset();
    char line[256];
    // Sin latidos no hay trabados (el slot nunca arrancó).
    assert(!stalled(Slot::Interp, 100000, 5000));
    assert(any_stalled(100000, 5000) == 0);

    // Periódico (interp60): trabado por edad aunque esté "fuera".
    mark(Slot::Interp, 2, 1000);
    assert(!stalled(Slot::Interp, 5999, 5000));
    assert(stalled(Slot::Interp, 6001, 5000));

    // Aperiódico (DDraw present del guest): menú quieto fuera de llamada no es trabado...
    mark(Slot::DdrawPresent, 4, 1000);
    assert(!stalled(Slot::DdrawPresent, 60000, 5000));
    // ...pero dentro de una llamada bloqueante (impar) sí.
    mark(Slot::DdrawPresent, 5, 1000);
    assert(stalled(Slot::DdrawPresent, 6001, 5000));
    assert(beat(Slot::DdrawPresent).count.load() == 2);

    const unsigned mask = any_stalled(6001, 5000);
    assert(mask == ((1u << unsigned(Slot::Interp)) | (1u << unsigned(Slot::DdrawPresent))));

    const size_t n = format(line, sizeof line, 6001, 5000);
    const std::string s(line, n);
    assert(s.rfind("hb t=6001", 0) == 0);
    assert(s.find("ddraw=5@5001#2*") != std::string::npos);
    assert(s.find("interp=2@5001#1*") != std::string::npos);
    assert(s.find("audio=-") != std::string::npos);  // sin latidos
    assert(n < sizeof line && line[n] == 0);

    // Buffer chico: trunca sin desbordar y termina en NUL.
    char tiny[12];
    const size_t m = format(tiny, sizeof tiny, 6001, 5000);
    assert(m < sizeof tiny && tiny[m] == 0);
    // Latido de otro core posterior a `now` (carrera de lectura): edad 0, no 2^32-k.
    reset();
    mark(Slot::Audio, 1, 10005);
    assert(!stalled(Slot::Audio, 10000, 5000));
    const size_t k = format(line, sizeof line, 10000, 5000);
    assert(std::string(line, k).find("audio=1@0#1") != std::string::npos);
    return 0;
}
