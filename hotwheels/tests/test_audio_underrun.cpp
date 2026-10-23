// tests/test_audio_underrun.cpp — BUG-031: productor en ráfagas (frame-tick del guest a
// ~15 fps con jitter) vs consumidor continuo (grano 512 @48k). Mide huecos (underruns).
#include "audio/dsound_bus.h"
#include "audio/mixer.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, m); ++g_fail; } } while (0)
using namespace hw::audio;

static int64_t g_fake_us = 0;
static DsoundBus::TimePoint fake_now() {
    return DsoundBus::TimePoint(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::microseconds(g_fake_us)));
}

// Seno continuo 22050 Hz como fuente: cualquier hueco/repetición se ve como discontinuidad.
static int run(DsoundBus& bus, int seconds, uint64_t& zero_runs) {
    Mixer mx; mx.add(&bus);
    const double w = 2 * M_PI * 440 / 22050;
    double ph = 0; int64_t src_clock_us = 0, next_prod = 0;
    uint32_t seed = 1;
    int64_t t_us = 0; const int64_t grain_us = 512 * 1000000LL / 48000;
    int64_t produced = 0;  // frames src ya entregados
    std::vector<int16_t> out(512 * 2);
    int zeros = 0; zero_runs = 0;
    for (; t_us < seconds * 1000000LL; t_us += grain_us) {
        g_fake_us = t_us;
        const bool warm = t_us > 1500000;  // el arranque (llenado inicial + adaptación) no cuenta
        // productor: cada ~66±30 ms entrega lo que corresponde al reloj de pared
        while (next_prod <= t_us) {
            int64_t target = next_prod * 22050 / 1000000;
            int n = (int)(target - produced);
            std::vector<int16_t> p((size_t)n * 2);
            for (int i = 0; i < n; ++i) { int16_t v = (int16_t)(8000 * std::sin(ph)); ph += w; p[2*i] = p[2*i+1] = v; }
            if (n > 0) bus.push(p.data(), n, 22050);
            produced += n;
            seed = seed * 1103515245u + 12345u;
            next_prod += 36000 + (int64_t)(seed >> 16) % 60000;
        }
        mx.mix(out.data(), 512);
        for (int i = 0; i < 512; ++i) {
            if (out[2*i] == 0) ++zeros; else { if (warm && zeros > 24) ++zero_runs; zeros = 0; }
        }
    }
    (void)src_clock_us;
    return (int)bus.underruns();
}
int main() {
    DsoundBus bus(kSampleRate * 4, &fake_now); // reloj virtual: stress determinista sin sleeps
    uint64_t gaps = 0;
    run(bus, 10, gaps);
    std::printf("huecos de silencio (>24 muestras en cero): %llu, underruns=%llu\n",
                (unsigned long long)gaps, (unsigned long long)bus.underruns());
    CHECK(gaps <= 2, "con prefill el jitter del productor no produce huecos audibles");
    if (g_fail) { std::printf("UR: %d FALLAS\n", g_fail); return 1; }
    std::printf("UR: PASS\n");
}
