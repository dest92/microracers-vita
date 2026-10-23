// CA2 SPEC-055: el muestreador del dt del invitado.
// El invitado escribe el dt ANTES que el contador previo; el muestreo lee el
// contador primero y el dt después, así que el dt registrado es el de la espera
// que acaba de terminar (y no el de la siguiente).
#include "runtime/guest_dt_profile.h"

#include <cstdio>
#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

using hw::runtime::GuestDtProfile;
using hw::runtime::GuestDtResult;
using hw::runtime::GuestDtSampler;

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, m); ++g_fail; } } while (0)

// Invitado falso: dos globales (dt y contador previo) con el orden de escritura
// real (dt primero, contador después).
struct FakeGuest {
    uint32_t dt = 0;
    uint64_t prev = 0;
    // Termina una espera: escribe dt y después el contador.
    void finish_wait(uint32_t new_dt, uint64_t new_prev) {
        dt = new_dt;
        prev = new_prev;
    }
    uint64_t read_prev() const { return prev; }
    uint32_t read_dt() const { return dt; }
};

static GuestDtSampler make_sampler(FakeGuest& guest) {
    return GuestDtSampler([&guest] { return guest.read_prev(); },
                          [&guest] { return guest.read_dt(); });
}

int main() {
    // 1) Una espera: se detecta el cambio del contador y el dt es el publicado.
    {
        FakeGuest guest;
        auto sampler = make_sampler(guest);
        sampler.sample();                       // línea base (contador 0)
        guest.finish_wait(0x1000 + 82, 1);      // dt = 4178, contador = 1
        sampler.sample();                       // ve el contador nuevo
        const GuestDtResult r = sampler.result();
        CHECK(r.waits == 1, "una espera detectada");
        CHECK(r.samples == 2, "dos muestras");
        CHECK(r.histogram.size() == 1 && r.histogram.at(0x1000 + 82) == 1, "dt en el histograma");
        CHECK(r.min_dt == 0x1000 + 82 && r.max_dt == 0x1000 + 82, "min/max");
        CHECK(r.out_of_range == 0, "dt en rango");
        CHECK(r.percentile(0.5) == 0x1000 + 82, "p50");
    }

    // 2) Cada espera registra SU dt (el de la que terminó, no el de la siguiente).
    {
        FakeGuest guest;
        auto sampler = make_sampler(guest);
        sampler.sample();
        guest.finish_wait(4100, 1);
        sampler.sample();  // detecta la espera 1
        guest.finish_wait(4200, 2);
        sampler.sample();  // detecta la espera 2
        const GuestDtResult r = sampler.result();
        CHECK(r.waits == 2, "dos esperas");
        CHECK(r.histogram.at(4100) == 1 && r.histogram.at(4200) == 1, "un dt por espera");
        CHECK(r.sum_dt == 4100 + 4200, "suma");
    }

    // 3) Invitado que no escribe nada: 0 esperas, ningún dt espurio.
    {
        FakeGuest guest;
        auto sampler = make_sampler(guest);
        for (int i = 0; i < 5; ++i) sampler.sample();
        const GuestDtResult r = sampler.result();
        CHECK(r.waits == 0, "sin esperas");
        CHECK(r.histogram.empty(), "histograma vacío");
        CHECK(r.percentile(0.5) == 0, "percentile sin datos");
        CHECK(r.mean() == 0.0, "media sin datos");
    }

    // 4) dt fuera del rango que garantiza el invitado: se cuenta, no se oculta.
    {
        FakeGuest guest;
        auto sampler = make_sampler(guest);
        sampler.sample();
        guest.finish_wait(0x20000, 1);
        sampler.sample();
        const GuestDtResult r = sampler.result();
        CHECK(r.waits == 1, "espera detectada");
        CHECK(r.out_of_range == 1, "fuera de rango");
        CHECK(r.distinct == 1, "un valor distinto");
    }

    // 5) Percentiles sobre una distribución conocida.
    {
        FakeGuest guest;
        auto sampler = make_sampler(guest);
        sampler.sample();
        uint64_t prev = 1;
        for (int i = 0; i < 10; ++i) {
            guest.finish_wait(4000 + uint32_t(i), prev++);
            sampler.sample();
        }
        const GuestDtResult r = sampler.result();
        CHECK(r.waits == 10, "diez esperas");
        CHECK(r.percentile(0.0) == 4000, "p0");
        CHECK(r.percentile(0.5) == 4004, "p50 (rango más cercano: el 5º de 10)");
        CHECK(r.percentile(1.0) == 4009, "p100");
    }

    // 6) El perfil con hilo arranca, muestrea y para sin colgarse.
    {
        FakeGuest guest;
        std::atomic<uint64_t> counter{0};
        GuestDtProfile profile([&] { return counter.fetch_add(1); },
                               [&guest] { return guest.read_dt(); });
        CHECK(profile.start(), "arranca");
        CHECK(profile.running(), "corriendo");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const GuestDtResult r = profile.stop();
        CHECK(!profile.running(), "parado");
        CHECK(r.waits >= 1, "detectó esperas");
        CHECK(r.samples >= 1, "muestreó");
    }

    if (g_fail) {
        std::printf("test_guest_dt_profile: %d fallo(s)\n", g_fail);
        return 1;
    }
    std::printf("test_guest_dt_profile: OK\n");
    return 0;
}
