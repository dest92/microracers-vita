#include "diag/thread_cpu.h"
#include <cassert>
#include <thread>
#include <array>
#include <cstring>
#include <atomic>

using namespace hw::diag;
int main() {
    ThreadSample a{}, b{};
    a.tid = b.tid = 17;
    a.generation = b.generation = 1;
    a.wall_us = 100; b.wall_us = 10100100;
    a.clocks = 3; b.clocks = (uint64_t(1) << 40) + 3;
    assert(thread_delta(a, b).status == DeltaStatus::Valid);
    assert(thread_delta(a, b).clocks == (uint64_t(1) << 40));
    b.clocks = a.clocks;
    assert(thread_delta(a, b).status == DeltaStatus::Valid);
    assert(thread_delta(a, b).clocks == 0);
    b.rc = -1;
    assert(thread_delta(a, b).status == DeltaStatus::ApiError);
    assert(thread_delta(a, b).clocks == 0);
    b.rc = 0; a.rc = -1;
    assert(thread_delta(a, b).status == DeltaStatus::ApiError);
    assert(thread_delta(a, b).clocks == 0);
    a.rc = 0; a.tid = 0;
    assert(thread_delta(a, b).status == DeltaStatus::Missing);
    a.tid = 17; b.tid = 0;
    assert(thread_delta(a, b).status == DeltaStatus::Missing);
    b.tid = 18;
    assert(thread_delta(a, b).status == DeltaStatus::ChangedTid);
    b.tid = 17; b.wall_us = a.wall_us;
    assert(thread_delta(a, b).status == DeltaStatus::TimeError);
    b.wall_us = a.wall_us - 1;
    assert(thread_delta(a, b).status == DeltaStatus::TimeError);
    b.wall_us = 10100100; b.clocks = 2;
    assert(thread_delta(a, b).status == DeltaStatus::CounterReset);
    // Un reset no puede reutilizar un delta válido anterior.
    assert(thread_delta(a, b).clocks == 0);
    b.clocks = a.clocks; b.generation = 2;
    assert(thread_delta(a, b).status == DeltaStatus::ChangedGeneration);
    char line[1200];
    for (const char* scene : {"menu-production", "race"}) {
        format_thread_window(line, sizeof line, "guest", scene, 800, 600, 0, 1, a, b);
        assert(std::strstr(line, "scene=unverified "));
        assert(std::strstr(line, scene));
        assert(std::strstr(line, "delta_valid=0"));
        assert(std::strstr(line, "delta_raw=unavailable"));
    }
    b.generation = a.generation; b.clocks = (uint64_t(1) << 40) + 3;
    b.mask = 0x20000; b.priority = 128;
    for (const char* scene : {"menu-production", "race"}) {
        format_thread_window(line, sizeof line, "guest", scene, 800, 600, 0, 1, a, b);
        assert(std::strstr(line, "scene=unverified "));
        assert(std::strstr(line, scene));
        assert(std::strstr(line, "delta_valid=1"));
        assert(std::strstr(line, "delta_raw=1099511627776"));
        assert(std::strstr(line, "affinity_changed=1 priority_changed=1"));
        assert(!std::strstr(line, "cpu_us"));
        assert(!std::strstr(line, "%"));
        assert(!std::strstr(line, "saturat"));
    }
    for (const auto size : {std::array<uint32_t, 2>{0, 0},
                            std::array<uint32_t, 2>{800, 0},
                            std::array<uint32_t, 2>{0, 600}}) {
        format_thread_window(line, sizeof line, "render", "race", size[0], size[1], 0, 1, a, b);
        assert(std::strstr(line, "size=unknown "));
    }
    ThreadRegistry registry;
    registry.set(0, 17);
    const auto first = registry.token(0);
    registry.set(0, 0); registry.set(0, 17);
    assert(first != registry.token(0));
    std::atomic<bool> stop{false};
    std::array<std::atomic<bool>, kThreadRoles> observed{};
    std::atomic<bool> reader_ready{false};
    std::thread reader([&] {
        reader_ready.store(true);
        while (!stop.load()) {
            for (unsigned i = 0; i < kThreadRoles; ++i) {
                const auto t = registry.token(i);
                if (uint32_t(t) == 100 + i) {
                    assert(uint32_t(t >> 32) > 0);
                    observed[i].store(true);
                } else if (i != 0) assert(uint32_t(t) == 0);
            }
        }
    });
    while (!reader_ready.load()) std::this_thread::yield();
    std::array<std::thread, kThreadRoles> workers;
    for (unsigned i = 0; i < kThreadRoles; ++i)
        workers[i] = std::thread([&, i] {
            registry.set(i, 100 + i);
            while (!observed[i].load()) std::this_thread::yield();
        });
    for (auto& worker : workers) worker.join();
    for (unsigned i = 0; i < kThreadRoles; ++i)
        assert(registry.get(i) == int(100 + i));
    stop.store(true); reader.join();
    registry.set(2, 0);
    assert(registry.get(2) == 0);
    std::puts("PASS thread_cpu: raw deltas, invalid endpoints, generations, scene and concurrent publication");
}
