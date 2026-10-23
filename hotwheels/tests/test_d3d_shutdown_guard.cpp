#include "host/d3d_shutdown_guard.h"

#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

struct State {
    std::mutex mutex;
    std::condition_variable ready;
    bool release = false;
    bool backend_alive = true;
    bool consumer_done = false;
};

struct Backend {
    explicit Backend(State& state) : state(state) {}
    ~Backend() {
        std::lock_guard<std::mutex> lock(state.mutex);
        assert(state.consumer_done && "backend freed before consumer join");
        state.backend_alive = false;
    }
    State& state;
};

int main() {
    State state;
    auto backend = std::make_unique<Backend>(state);
    std::thread consumer([&] {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.ready.wait(lock, [&] { return state.release; });
        assert(state.backend_alive); // modelo del acceso al Backend/ring capturado
        state.consumer_done = true;
    });
    {
        // Same declaration order as host_main: guard after Backend => guard
        // closes/drains and joins before Backend's destructor.
        hw::host::D3DShutdownGuard shutdown([&] {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.release = true;
            }
            state.ready.notify_all();
            consumer.join();
        });
    }
    backend.reset();
    assert(state.consumer_done);
}
