// HW-036: bursts finitos salen dentro del target del reloj monotónico.
#include "audio/dsound_bus.h"
#include "audio/mixer.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using hw::audio::DsoundBus;
using hw::audio::Mixer;
using Clock = std::chrono::steady_clock;

static int64_t g_fake_us = 0;
static DsoundBus::TimePoint fake_now() {
    return DsoundBus::TimePoint(std::chrono::duration_cast<Clock::duration>(
        std::chrono::microseconds(g_fake_us)));
}

struct EmptySource : hw::audio::PcmSource {
    int reads = 0;
    int read(int16_t*, int) override { ++reads; return 0; }
    int rate() const override { return hw::audio::kSampleRate; }
};

static bool mix_has_signal(Mixer& mixer, std::vector<int16_t>& out) {
    mixer.mix(out.data(), 512);
    for (int16_t sample : out) if (sample != 0) return true;
    return false;
}

static void next_mix_block() { g_fake_us += 512 * 1000000LL / 48000; }

static bool check_empty_poll_limit() {
    g_fake_us = 0;
    EmptySource source;
    Mixer mixer;
    mixer.add(&source);
    std::vector<int16_t> out(512 * 2);
    mixer.mix(out.data(), 512);
    const bool once_first = source.reads == 1;
    mixer.mix(out.data(), 512);
    const bool once_next = source.reads == 2;
    std::printf("empty source reads/block=1,1: %d,%d\n", once_first ? 1 : 0,
                once_next ? 1 : 0);
    return once_first && once_next;
}

static bool check_burst(int source_ms) {
    constexpr int rate = 22050;
    const int count = rate * source_ms / 1000;
    g_fake_us = 0;
    DsoundBus bus(hw::audio::kSampleRate * 4, &fake_now);
    Mixer mixer;
    mixer.add(&bus);
    std::vector<int16_t> pcm(static_cast<size_t>(count) * 2, 7000);
    std::vector<int16_t> out(512 * 2);

    // Los bloques sin datos no inician el timer ni deben acortarlo.
    mix_has_signal(mixer, out);
    next_mix_block();
    mix_has_signal(mixer, out);
    bus.push(pcm.data(), count, rate);
    const int64_t start_us = g_fake_us;
    const bool early = mix_has_signal(mixer, out);

    bool emitted = false;
    int blocks = 0;
    while (!emitted && g_fake_us - start_us <= 250000) {
        next_mix_block();
        emitted = mix_has_signal(mixer, out);
        ++blocks;
    }
    const int64_t elapsed_us = g_fake_us - start_us;
    std::printf("burst=%dms early=%d emitted=%d blocks=%d elapsed=%lldus\n", source_ms,
                early ? 1 : 0, emitted ? 1 : 0, blocks,
                static_cast<long long>(elapsed_us));
    return !early && emitted && elapsed_us >= 120000 && elapsed_us <= 150000;
}

static bool check_adapted_burst() {
    constexpr int rate = 22050;
    g_fake_us = 0;
    DsoundBus bus(hw::audio::kSampleRate * 4, &fake_now);
    bus.set_prime_ms(360);
    std::vector<int16_t> warm(static_cast<size_t>(9000) * 2, 1000);
    std::vector<int16_t> short_pcm(static_cast<size_t>(110) * 2, 7000); // 5 ms, <512 frames
    std::vector<int16_t> scratch(static_cast<size_t>(4096) * 2);
    bus.push(warm.data(), 9000, rate);
    bus.read(scratch.data(), 4096);
    bus.read(scratch.data(), 4096);
    bus.read(scratch.data(), 4096); // pull parcial: target adaptativo 400 ms
    if (bus.underruns() != 1) return false;

    Mixer mixer;
    mixer.add(&bus);
    std::vector<int16_t> out(512 * 2);
    mix_has_signal(mixer, out); // vacío tras el underrun; no corre el timer
    next_mix_block();
    bus.push(short_pcm.data(), 110, rate);
    const int64_t start_us = g_fake_us;
    const bool early = mix_has_signal(mixer, out);
    bool emitted = false;
    int blocks = 0;
    while (!emitted && g_fake_us - start_us <= 450000) {
        next_mix_block();
        emitted = mix_has_signal(mixer, out);
        ++blocks;
    }
    const int64_t elapsed_us = g_fake_us - start_us;
    std::printf("adapted 5ms early=%d emitted=%d blocks=%d elapsed=%lldus\n",
                early ? 1 : 0, emitted ? 1 : 0, blocks,
                static_cast<long long>(elapsed_us));
    return !early && emitted && elapsed_us >= 400000 && elapsed_us <= 450000;
}

static bool check_exact_drain_rearms() {
    constexpr int rate = 22050;
    g_fake_us = 0;
    DsoundBus bus(hw::audio::kSampleRate * 4, &fake_now);
    std::vector<int16_t> full(static_cast<size_t>(4096) * 2, 1000);
    std::vector<int16_t> short_pcm(static_cast<size_t>(110) * 2, 7000);
    std::vector<int16_t> out(static_cast<size_t>(4096) * 2);
    bus.push(full.data(), 4096, rate);
    const int first = bus.read(out.data(), 4096);
    bus.push(short_pcm.data(), 110, rate);
    const int before_deadline = bus.read(out.data(), 4096);
    g_fake_us += 120000;
    const int after_deadline = bus.read(out.data(), 4096);
    std::printf("exact drain first=%d early=%d after120ms=%d\n", first,
                before_deadline, after_deadline);
    return first == 4096 && before_deadline == 0 && after_deadline == 110;
}

int main() {
    const bool poll = check_empty_poll_limit();
    const bool ok100 = check_burst(100);
    const bool ok5 = check_burst(5);
    const bool ok40 = check_burst(40);
    const bool ok_adapted = check_adapted_burst();
    const bool exact_rearm = check_exact_drain_rearms();
    if (!poll || !ok100 || !ok5 || !ok40 || !ok_adapted || !exact_rearm) {
        std::puts("SHORT_BURST: FAIL");
        return 1;
    }
    std::puts("SHORT_BURST: PASS");
    return 0;
}
