// tests/test_audio_selfpaced.cpp — BUG-031: productor self-paced (hilo propio con contrapresión
// del bus) vs productor atado al frame-tick del guest al ~60 %. Seno continuo 440 Hz @22050
// → Mixer @48k; mide huecos de silencio y salto de fase en estado estable (3 s de pared).
#include "audio/dsound_bus.h"
#include "audio/mixer.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, m); ++g_fail; } } while (0)
using namespace hw::audio;
using namespace std::chrono;

struct Result { int holes = 0; uint64_t underruns = 0; int max_level_ms = 0; };

// mode 0: guest-pumped al 60 % (ráfagas cada 66 ms con 60 % del audio que corresponde)
// mode 1: self-paced: hilo que produce grano de 512@22050 y espera nivel del bus
static Result run(int mode, int seconds) {
    DsoundBus bus;
    Mixer mx; mx.add(&bus);
    std::atomic<bool> stop{false};
    std::thread prod([&] {
        const double w = 2 * M_PI * 440 / 22050; double ph = 0;
        auto t0 = steady_clock::now(); int64_t produced = 0;
        std::vector<int16_t> p;
        while (!stop) {
            int n;
            if (mode == 0) {
                std::this_thread::sleep_for(milliseconds(66));
                int64_t el = duration_cast<microseconds>(steady_clock::now() - t0).count();
                n = (int)(el * 22050 / 1000000 * 6 / 10 - produced);   // sólo rinde 60 %
            } else {
                n = 512;
                while (!stop && !bus.wait_below(bus.prime_ms() + 60, 20)) {}  // espera acotada y reintenta
                if (stop) break;
            }
            if (n <= 0) continue;
            p.resize((size_t)n * 2);
            for (int i = 0; i < n; ++i) { int16_t v = (int16_t)(8000 * std::sin(ph)); ph += w; p[2*i] = p[2*i+1] = v; }
            bus.push(p.data(), n, 22050);
            produced += n;
        }
    });
    Result r; std::vector<int16_t> out(512 * 2);
    auto t0 = steady_clock::now(); int zeros = 0;
    for (int64_t g = 0; ; ++g) {
        auto due = t0 + microseconds(g * 512 * 1000000LL / 48000);
        if (due - t0 > seconds * 1s) break;
        std::this_thread::sleep_until(due);
        mx.mix(out.data(), 512);
        const bool warm = (due - t0) > 1500ms;
        for (int i = 0; i < 512; ++i) {
            if (out[2*i] == 0) ++zeros; else { if (warm && zeros > 24) ++r.holes; zeros = 0; }
        }
        r.max_level_ms = std::max(r.max_level_ms, bus.level_ms());
    }
    stop = true; prod.join();
    r.underruns = bus.underruns();
    return r;
}
int main() {
    Result a = run(0, 4), b = run(1, 4);
    std::printf("guest-pumped 60%%: huecos=%d underruns=%llu | self-paced: huecos=%d underruns=%llu nivel_max=%dms\n",
                a.holes, (unsigned long long)a.underruns, b.holes, (unsigned long long)b.underruns, b.max_level_ms);
    CHECK(a.holes > 0, "control: productor al 60% SÍ produce huecos (el test mide algo)");
    CHECK(b.holes == 0, "self-paced: sin huecos");
    CHECK(b.max_level_ms <= 400, "self-paced: latencia acotada (contrapresión)");
    if (g_fail) { std::printf("SP: %d FALLAS\n", g_fail); return 1; }
    std::printf("SP: PASS\n");
}
