#include "integration/audio_metrics_gate.h"

#include <cstdio>

int main() {
    hw::integration::AudioMetricsGate gate;
    if (!gate.due(1000)) return 1;          // primera trama produce baseline
    if (gate.due(10999)) return 2;          // no más de una línea / 10 s
    if (!gate.due(11000)) return 3;         // límite exacto incluido
    if (gate.due(20999)) return 4;
    if (!gate.due(21000)) return 5;
    std::puts("audio metrics gate: PASS");
    return 0;
}
