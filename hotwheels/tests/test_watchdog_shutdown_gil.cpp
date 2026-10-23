#include "runtime/gil.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "host/watchdog_shutdown.h"

namespace {
void check(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL watchdog GIL: %s\n", what);
    std::exit(1);
}

struct ProbeScheduler {
    int requests = 0;
    void request_shutdown() {
        // A nested try-lock must fail exactly because the helper owns the GIL.
        const bool stole = d2rt::gil::try_lock();
        if (stole) d2rt::gil::unlock();
        check(!stole, "request called outside GIL ownership");
        ++requests;
    }
};

void test_immediate() {
    ProbeScheduler s;
    unsigned pauses = 0;
    check(hw::host::request_shutdown_bounded(&s, 1, [&] { ++pauses; }), "immediate acquire");
    check(s.requests == 1 && pauses == 0, "one request without retry");
    check(d2rt::gil::try_lock(), "helper releases GIL");
    d2rt::gil::unlock();
}

void test_retry_after_contention() {
    ProbeScheduler s;
    std::atomic<bool> held{false}, release{false};
    std::thread holder([&] {
        d2rt::gil::lock();
        held.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        d2rt::gil::unlock();
    });
    while (!held.load(std::memory_order_acquire)) std::this_thread::yield();
    unsigned pauses = 0;
    const bool requested = hw::host::request_shutdown_bounded(&s, 100, [&] {
        if (++pauses == 3) release.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    holder.join();
    check(requested && s.requests == 1 && pauses >= 3, "retry acquires released GIL");
    check(d2rt::gil::try_lock(), "retry path releases GIL");
    d2rt::gil::unlock();
}

void test_exhausted_contention() {
    ProbeScheduler s;
    std::atomic<bool> held{false}, release{false};
    std::thread holder([&] {
        d2rt::gil::lock();
        held.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        d2rt::gil::unlock();
    });
    while (!held.load(std::memory_order_acquire)) std::this_thread::yield();
    unsigned pauses = 0;
    const bool requested = hw::host::request_shutdown_bounded(&s, 5, [&] { ++pauses; });
    release.store(true, std::memory_order_release);
    holder.join();
    check(!requested && s.requests == 0 && pauses == 5, "exhaustion leaves shutdown to fallback");
    check(d2rt::gil::try_lock(), "exhaustion path leaves GIL available");
    d2rt::gil::unlock();
}

// ---- BUG-054: presupuesto de apagado por PROGRESO del invitado --------------
// El watchdog no debe decidir por un plazo fijo de tiempo de pared (mide el
// starvation del planificador, no la liveness del invitado): renueva el
// presupuesto mientras el invitado progrese y fuerza la salida sólo tras una
// ventana sin progreso.

struct FakeClock {
    uint64_t ms = 0;
    uint64_t now_ms() const { return ms; }
    void advance(std::chrono::milliseconds d) { ms += uint64_t(d.count()); }
};

// Sonda falsa: `acq` (adquisiciones del GIL) avanza `progress_calls` veces y
// después se congela: el invitado deja de alcanzar safepoints.
struct FakeProbe {
    uint32_t acq = 0;
    int progress_calls = 0;
    int calls = 0;
    void fill(d2rt::gil::Probe& p) {
        p.active = true;
        if (calls < progress_calls) ++acq;
        ++calls;
        p.acq = acq;
    }
};

void test_progress_renews_budget() {
    hw::host::ShutdownWatchdog wd;
    wd.hang_ms = 1000;
    wd.begin(0);
    // El invitado progresa cada 100 ms: la espera sigue viva aunque el tiempo
    // total supere la ventana (que se renueva con cada progreso). El tope duro
    // (ventana + 3 s) queda para test_hard_cap.
    for (uint64_t t = 100; t <= 3900; t += 100) {
        d2rt::gil::Probe p;
        p.active = true;
        p.acq = uint32_t(t / 100);
        check(wd.sample(t, p, false), "el progreso renueva el presupuesto");
    }
}

void test_no_progress_forces_exit() {
    hw::host::ShutdownWatchdog wd;
    wd.hang_ms = 1000;
    wd.begin(0);
    d2rt::gil::Probe p;
    p.active = true;
    p.acq = 7;
    check(wd.sample(0, p, false), "la primera muestra es línea base");
    check(wd.sample(999, p, false), "dentro de la ventana se sigue esperando");
    check(!wd.sample(1000, p, false), "sin progreso tras la ventana se fuerza la salida");
}

void test_own_acquisition_is_not_progress() {
    hw::host::ShutdownWatchdog wd;
    wd.hang_ms = 1000;
    wd.begin(0);
    d2rt::gil::Probe p;
    p.active = true;
    p.acq = 5;
    check(wd.sample(0, p, false), "línea base");
    // El watchdog toma el GIL para entregar el pedido de apagado: esa
    // adquisición NO es progreso del invitado, así que no renueva.
    p.acq = 6;
    check(wd.sample(900, p, true), "la adquisición propia no fuerza dentro de la ventana");
    check(!wd.sample(1900, p, false), "sin progreso del invitado se fuerza la salida");
}

void test_hard_cap() {
    hw::host::ShutdownWatchdog wd;
    wd.hang_ms = 1000;
    wd.begin(0);
    for (uint64_t t = 100; t < 4000; t += 100) {
        d2rt::gil::Probe p;
        p.active = true;
        p.acq = uint32_t(t / 100);
        check(wd.sample(t, p, false), "el progreso no fuerza la salida por la ventana");
    }
    d2rt::gil::Probe p;
    p.active = true;
    p.acq = 100;
    check(!wd.sample(4000, p, false), "el tope duro corta aunque el invitado progrese");
}

void test_real_gil_progress_is_visible() {
    d2rt::gil::Probe a, b;
    d2rt::gil::probe(&a);
    check(a.active, "el GIL está activo en el test");
    { d2rt::gil::lock(); d2rt::gil::unlock(); }
    d2rt::gil::probe(&b);
    check(b.acq > a.acq, "una adquisición del GIL avanza el contador que ve el watchdog");
    check(!b.held, "probe() no deja el GIL tomado");
}

void test_loop_waits_while_guest_progresses() {
    FakeClock clk;
    FakeProbe probe;
    probe.progress_calls = 10;
    ProbeScheduler sched;
    hw::host::ShutdownWatchdog wd;
    wd.hang_ms = 1000;
    wd.begin(clk.now_ms());
    bool forced = false;
    hw::host::await_shutdown(
        &sched, wd, true,
        [&] { return clk.now_ms(); },
        [&](std::chrono::milliseconds d) { clk.advance(d); },
        [&](d2rt::gil::Probe& p) { probe.fill(p); },
        [&] { forced = true; });
    check(forced, "el lazo termina forzando la salida");
    // Con progreso el lazo esperó mucho más que la ventana sin progreso: si
    // el presupuesto no se renovara, habría forzado a los 1000 ms.
    check(clk.now_ms() > 3000, "el progreso renovó el presupuesto");
}

void test_loop_forces_without_progress() {
    FakeClock clk;
    FakeProbe probe;
    ProbeScheduler sched;
    hw::host::ShutdownWatchdog wd;
    wd.hang_ms = 1000;
    wd.begin(clk.now_ms());
    bool forced = false;
    hw::host::await_shutdown(
        &sched, wd, true,
        [&] { return clk.now_ms(); },
        [&](std::chrono::milliseconds d) { clk.advance(d); },
        [&](d2rt::gil::Probe& p) { probe.fill(p); },
        [&] { forced = true; });
    check(forced, "sin progreso el lazo fuerza la salida");
    check(clk.now_ms() >= 1000, "el lazo respetó la ventana sin progreso");
}

void test_loop_delivers_pending_request() {
    FakeClock clk;
    FakeProbe probe;
    ProbeScheduler sched;
    hw::host::ShutdownWatchdog wd;
    wd.begin(clk.now_ms());
    hw::host::await_shutdown(
        &sched, wd, false,  // el pedido inicial NO se entregó: hay que reintentar
        [&] { return clk.now_ms(); },
        [&](std::chrono::milliseconds d) { clk.advance(d); },
        [&](d2rt::gil::Probe& p) { probe.fill(p); },
        [] {});
    check(sched.requests == 1, "el lazo entrega el pedido de apagado que no se pudo entregar");
}
} // namespace

int main() {
    // El GIL publica su observabilidad (probe) sólo cuando está activo: lo
    // activa el constructor del NativeScheduler, que este test no construye.
    d2rt::gil::enable();
    test_immediate();
    test_retry_after_contention();
    test_exhausted_contention();
    test_progress_renews_budget();
    test_no_progress_forces_exit();
    test_own_acquisition_is_not_progress();
    test_hard_cap();
    test_real_gil_progress_is_visible();
    test_loop_waits_while_guest_progresses();
    test_loop_forces_without_progress();
    test_loop_delivers_pending_request();
    std::puts("PASS watchdog GIL: immediate, retry, exhaustion, progreso (BUG-054)");
    return 0;
}
