#pragma once

#include <functional>
#include <utility>

namespace hw::host {

// Scope guard for globals that own worker threads using a caller-owned D3D
// backend. Declare it after the backend; reverse local destruction then joins
// the worker before the backend is freed, including on early returns.
class D3DShutdownGuard {
public:
    explicit D3DShutdownGuard(std::function<void()> shutdown)
        : shutdown_(std::move(shutdown)) {}
    D3DShutdownGuard(const D3DShutdownGuard&) = delete;
    D3DShutdownGuard& operator=(const D3DShutdownGuard&) = delete;
    ~D3DShutdownGuard() noexcept {
        if (shutdown_) shutdown_();
    }

private:
    std::function<void()> shutdown_;
};

} // namespace hw::host
