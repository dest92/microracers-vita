#pragma once

#include "runtime/gil.h"

#include <chrono>
#include <thread>
#include <utility>

namespace hw { namespace host {

// request_shutdown mutates scheduler state protected by the global GIL.
// Keep acquisition non-blocking and bounded so a wedged guest still reaches
// the watchdog's forced-exit fallback.
template<class Scheduler, class Pause>
bool request_shutdown_bounded(Scheduler* scheduler, unsigned attempts, Pause&& pause) {
    if (!scheduler) return false;
    for (unsigned i = 0; i < attempts; ++i) {
        if (!d2rt::gil::try_lock()) {
            pause();
            continue;
        }
        struct Unlock { ~Unlock() { d2rt::gil::unlock(); } } unlock;
        scheduler->request_shutdown();
        return true;
    }
    return false;
}

template<class Scheduler>
bool request_shutdown_bounded(Scheduler* scheduler, unsigned attempts) {
    return request_shutdown_bounded(scheduler, attempts, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
}

// ---- Shutdown budget renewed by GUEST PROGRESS, not by wall clock (BUG-054)
//
// The shutdown is cooperative: the watchdog asks for it and the guest has to
// reach a safepoint (an import trap) to release the GIL and see the request.
// A FIXED wall-clock budget measures the scheduler's starvation, not the
// guest's liveness: under host oversubscription a healthy guest may not be
// dispatched for longer than the budget, and the watchdog kills it (a
// spurious FAIL of the smoke gate).
//
// Progress signal: the GIL's acquisition counter (gil::Probe::acq). The guest
// takes the GIL on every import trap, and a trap is exactly the point where
// the scheduler can observe the shutdown request: if acq advances, the guest
// is back where it can see the request. probe() never blocks (it does not
// take the GIL), which is required: the GIL is the contended resource being
// observed.
struct ShutdownWatchdog {
    // No progress during this window => the guest will not reach a safepoint
    // => wedged => force the exit. Hard cap: even a guest that keeps
    // progressing but never stops is cut off (the gate must not hang).
    static constexpr uint32_t kPollMs = 250;
    static constexpr uint32_t kDefaultHangMs = 12000;
    static constexpr uint32_t kCapGraceMs = 3000;

    uint32_t hang_ms = kDefaultHangMs;
    uint64_t start_ms = 0;
    uint64_t last_progress_ms = 0;
    uint32_t last_acq = 0;
    bool have_sample = false;

    void begin(uint64_t now_ms) {
        start_ms = last_progress_ms = now_ms;
        have_sample = false;
    }

    // Samples the progress and decides. Returns true while the watchdog must
    // keep waiting; false => force the exit.
    // `own_acq` marks a sample taken right after the watchdog itself took the
    // GIL (to deliver the request): that acquisition is not guest progress, so
    // it must not renew the budget.
    bool sample(uint64_t now_ms, const d2rt::gil::Probe& p, bool own_acq) {
        const uint32_t acq = p.active ? p.acq : 0u;  // no GIL (coop): no signal
        if (!have_sample) {
            have_sample = true;
            last_acq = acq;
            last_progress_ms = now_ms;
            return true;
        }
        if (!own_acq && acq != last_acq) last_progress_ms = now_ms;  // progress
        last_acq = acq;
        if (now_ms - last_progress_ms >= hang_ms) return false;      // no progress
        if (now_ms - start_ms >= uint64_t(hang_ms) + kCapGraceMs) return false;
        return true;
    }
};

// Waits for the guest to stop, renewing the budget with its progress.
// `delivered` says whether the shutdown request was already delivered; if it
// was not, it is retried (one bounded attempt per poll) until it is: a
// request that never reaches the guest means the guest never stops.
// `force()` is called when the guest must be killed.
template<class Scheduler, class Now, class Sleep, class ProbeFn, class Force>
void await_shutdown(Scheduler* ns, ShutdownWatchdog& wd, bool delivered,
                    Now now_ms, Sleep sleep, ProbeFn probe, Force force) {
    for (;;) {
        sleep(std::chrono::milliseconds(ShutdownWatchdog::kPollMs));
        d2rt::gil::Probe p{};
        probe(p);
        bool own_acq = false;
        if (!delivered) {
            own_acq = request_shutdown_bounded(ns, 1);
            delivered = own_acq;
        }
        if (!wd.sample(now_ms(), p, own_acq)) {
            force();
            return;
        }
    }
}

}} // namespace hw::host
