// Host microbenchmark for HW-035. Use TSC ticks on x86; never treat them as A9 cycles.
#include "fps/interp60.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <time.h>
#include <vector>

namespace {
thread_local bool count_allocations = false;
thread_local uint64_t allocations = 0, allocated_bytes = 0;
uint64_t cycles() {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile("lfence\n\trdtsc\n\tlfence" : "=a"(lo), "=d"(hi) :: "memory");
    return (uint64_t(hi) << 32) | lo;
#else
    return 0;
#endif
}
uint64_t thread_cpu_ns() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}
uint32_t bits(float value) { uint32_t out; std::memcpy(&out, &value, 4); return out; }
hw::d3d::Ring make_frame(uint32_t frame_no, uint32_t draws = 765,
                         uint32_t texture_churn = 0) {
    using hw::d3d::Op;
    hw::d3d::Ring ring;
    for (uint32_t id = 0; id < texture_churn; ++id)
        ring.put(Op::TexUpload, {100000u + id});
    ring.put(Op::Begin, {800, 600});
    ring.put(Op::Viewport, {0, 0, 800, 600});
    std::vector<uint32_t> payload;
    for (uint32_t i = 0; i < draws; ++i) {
        const uint32_t vertices = (i % 4 == 0) ? 4 : 3;
        payload.assign(2 + vertices * 6, 0);
        payload[0] = 5; payload[1] = vertices;
        for (uint32_t v = 0; v < vertices; ++v) {
            const size_t p = 2 + v * 6;
            payload[p] = bits(float(i % 800) + float(frame_no) * 0.25f + float(v));
            payload[p + 1] = bits(float(i % 600) + float(frame_no) * 0.125f + float(v));
            payload[p + 2] = bits(0.5f); payload[p + 3] = bits(1.f);
            payload[p + 4] = bits(float(v) / vertices); payload[p + 5] = bits(0.5f);
        }
        ring.put(Op::Texture, {0, (i % 32) + 1});
        ring.put(Op::Draw, payload.data(), uint32_t(payload.size()));
    }
    ring.put(Op::End, {});
    ring.put(Op::Present, {});
    return ring;
}
uint64_t percentile(std::vector<uint64_t> values, size_t p) {
    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) * p / 100];
}
void print(const char* label, const std::vector<uint64_t>& samples) {
    std::printf("%s_tsc_ticks p50=%llu p95=%llu\n", label,
        (unsigned long long)percentile(samples, 50),
        (unsigned long long)percentile(samples, 95));
}
void print_ns(const char* label, const std::vector<uint64_t>& samples) {
    std::printf("%s_thread_cpu_ns p50=%llu p95=%llu\n", label,
        (unsigned long long)percentile(samples, 50),
        (unsigned long long)percentile(samples, 95));
}
}

void* operator new(std::size_t n) {
    if (count_allocations) { ++allocations; allocated_bytes += n; }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

struct Timings {
    std::vector<uint64_t> tsc, cpu_ns;
    uint64_t allocations = 0, bytes = 0;
    explicit Timings(size_t count) { tsc.reserve(count); cpu_ns.reserve(count); }
};

bool measure_capture(hw::fps::Interp60& interp, const std::shared_ptr<hw::d3d::Ring>& frame,
                    uint64_t timestamp, const std::vector<uint32_t>& worlds, bool fast,
                    Timings& result) {
    count_allocations = true; allocations = allocated_bytes = 0;
    const uint64_t cpu0 = thread_cpu_ns(), t0 = cycles();
    const bool ok = fast ? interp.capture_world_frame(frame, timestamp)
                         : interp.capture(*frame, timestamp, worlds);
    const uint64_t t1 = cycles(), cpu1 = thread_cpu_ns(); count_allocations = false;
    result.tsc.push_back(t1 - t0); result.cpu_ns.push_back(cpu1 - cpu0);
    result.allocations += allocations; result.bytes += allocated_bytes;
    return ok;
}

bool measure_tick(hw::fps::Interp60& interp, uint64_t now, hw::d3d::Ring& output,
                  hw::fps::InterpStats& stats, Timings& result) {
    count_allocations = true; allocations = allocated_bytes = 0;
    const uint64_t cpu0 = thread_cpu_ns(), t0 = cycles();
    const bool ok = interp.tick(now, output, &stats);
    const uint64_t t1 = cycles(), cpu1 = thread_cpu_ns(); count_allocations = false;
    result.tsc.push_back(t1 - t0); result.cpu_ns.push_back(cpu1 - cpu0);
    result.allocations += allocations; result.bytes += allocated_bytes;
    return ok;
}

int main(int argc, char** argv) {
    using namespace hw::fps;
    const uint32_t draws = argc > 1 ? uint32_t(std::strtoul(argv[1], nullptr, 10)) : 765;
    const uint32_t iterations = argc > 2 ? uint32_t(std::strtoul(argv[2], nullptr, 10)) : 400;
    const uint32_t texture_churn = argc > 3 ? uint32_t(std::strtoul(argv[3], nullptr, 10)) : 0;
    if (texture_churn > 384) {
        std::fprintf(stderr, "texture churn exceeds bounded per-frame capacity (384)\n");
        return 2;
    }
    Timings compat_capture(iterations), fast_capture(iterations),
            compat_tick(iterations), fast_tick(iterations), classify(iterations);
    Interp60 compatibility, zero_copy;
    hw::d3d::Ring compat_out, fast_out;
    uint64_t timestamp = 1000000;
    for (uint32_t i = 0; i < 8; ++i) {
        auto frame = std::make_shared<hw::d3d::Ring>(make_frame(i, draws, texture_churn));
        auto worlds = d3d_world_draw_ordinals(*frame);
        (void)compatibility.capture(*frame, timestamp, worlds);
        (void)zero_copy.capture_world_frame(frame, timestamp);
        timestamp += 25000;
        if (i > 0) {
            (void)compatibility.tick(timestamp - 12500, compat_out);
            (void)zero_copy.tick(timestamp - 12500, fast_out);
        }
    }

    uint64_t temp_bytes = 0, ring_in_bytes = 0, ring_out_bytes = 0;
    uint64_t matched = 0;
    for (uint32_t i = 8; i < 8 + iterations; ++i) {
        auto frame = std::make_shared<hw::d3d::Ring>(make_frame(i, draws, texture_churn));
        const uint64_t frame_bytes = uint64_t(frame->words().size()) * sizeof(uint32_t);
        count_allocations = true; allocations = allocated_bytes = 0;
        const uint64_t classify_cpu0 = thread_cpu_ns(), classify_t0 = cycles();
        auto worlds = d3d_world_draw_ordinals(*frame);
        const uint64_t classify_t1 = cycles(), classify_cpu1 = thread_cpu_ns(); count_allocations = false;
        classify.tsc.push_back(classify_t1 - classify_t0);
        classify.cpu_ns.push_back(classify_cpu1 - classify_cpu0);
        classify.allocations += allocations; classify.bytes += allocated_bytes;
        (i & 1 ? measure_capture(zero_copy, frame, timestamp, worlds, true, fast_capture)
               : measure_capture(compatibility, frame, timestamp, worlds, false, compat_capture));
        (i & 1 ? measure_capture(compatibility, frame, timestamp, worlds, false, compat_capture)
               : measure_capture(zero_copy, frame, timestamp, worlds, true, fast_capture));
        InterpStats a{}, b{};
        const uint64_t now = timestamp + 12500;
        const bool ok_a = (i & 1) ? measure_tick(zero_copy, now, fast_out, b, fast_tick)
                                  : measure_tick(compatibility, now, compat_out, a, compat_tick);
        const bool ok_b = (i & 1) ? measure_tick(compatibility, now, compat_out, a, compat_tick)
                                  : measure_tick(zero_copy, now, fast_out, b, fast_tick);
        if (!ok_a || !ok_b || compat_out.words() != fast_out.words() ||
            a.interpolated != b.interpolated || a.fallback != b.fallback) {
            std::fprintf(stderr, "compat/fast divergence at frame %u\n", i); return 2;
        }
        matched += b.interpolated;
        ring_in_bytes += frame_bytes;
        ring_out_bytes += fast_out.words().size() * sizeof(uint32_t);
        timestamp += 25000;
    }
    const uint64_t draw_words = uint64_t(draws) * 20 + ((draws + 3) / 4) * 6;
    if (matched == uint64_t(iterations) * draws) temp_bytes = draw_words * sizeof(uint32_t) * iterations;
    std::printf("HW-035 paired draws=%u iterations=%u vertices=3/4 unique_texture_updates=%u\n",
                draws, iterations, texture_churn);
    print("classify", classify.tsc); print_ns("classify", classify.cpu_ns);
    print("compat_capture", compat_capture.tsc); print_ns("compat_capture", compat_capture.cpu_ns);
    print("fast_capture", fast_capture.tsc); print_ns("fast_capture", fast_capture.cpu_ns);
    print("compat_tick", compat_tick.tsc); print_ns("compat_tick", compat_tick.cpu_ns);
    print("fast_tick", fast_tick.tsc); print_ns("fast_tick", fast_tick.cpu_ns);
    std::printf("allocs/frame classify=%llu compat_capture=%llu fast_capture=%llu compat_tick=%llu fast_tick=%llu\n",
        (unsigned long long)(classify.allocations / iterations),
        (unsigned long long)(compat_capture.allocations / iterations),
        (unsigned long long)(fast_capture.allocations / iterations),
        (unsigned long long)(compat_tick.allocations / iterations),
        (unsigned long long)(fast_tick.allocations / iterations));
    std::printf("copy_bytes/frame compat_input=%llu fast_input=0 output=%llu compat_temp=%llu fast_temp=0\n",
        (unsigned long long)(ring_in_bytes / iterations), (unsigned long long)(ring_out_bytes / iterations),
        (unsigned long long)(temp_bytes / iterations));
    return fast_capture.allocations || fast_tick.allocations ? 3 : 0;
}
