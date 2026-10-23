// guest_dt_profile.h — HW-072 (SPEC-055 CA2): observa desde el host el `dt` que
// el invitado usa para el frame.
//
// El invitado (HotWheels.exe) calcula su `dt` en timerWaitFrame40Hz (0x412df0):
// espera 25 ms girando sobre QueryPerformanceCounter y publica el resultado en
// dos globales de su .data (ImageBase 0x400000):
//   +0x68f8c → dt en punto fijo 4.12 (0x1000 = 25 ms)
//   +0x68f70 → contador QPC previo (lo actualiza al TERMINAR cada espera)
// El conteo de esperas sale de los cambios del contador previo; el `dt` se lee
// del global. Es observación pura: no se escribe nada en el invitado.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <thread>

namespace hw::runtime {

// RVA de los globales del PE del invitado (base fija 0x400000, .data en 0x45f000).
inline constexpr uint32_t kGuestDtRva = 0x68f8c;
inline constexpr uint32_t kGuestPrevRva = 0x68f70;

// Rango que el propio invitado garantiza: dt ∈ [0x1000, 0xf000].
inline constexpr uint32_t kGuestDtMin = 0x1000;
inline constexpr uint32_t kGuestDtMax = 0xf000;

struct GuestDtResult {
    uint64_t samples = 0;      // lecturas del dt
    uint64_t waits = 0;        // esperas detectadas (cambio del contador previo)
    uint32_t min_dt = 0;
    uint32_t max_dt = 0;
    uint64_t sum_dt = 0;
    uint32_t out_of_range = 0; // dt fuera de [0x1000, 0xf000]
    uint32_t distinct = 0;     // valores distintos de dt
    std::map<uint32_t, uint64_t> histogram; // dt → esperas
    uint32_t percentile(double p) const;   // 0..1; 0 si no hay esperas
    double mean() const { return waits ? double(sum_dt) / double(waits) : 0.0; }
};

// Acumula la distribución del dt. Los lectores se inyectan para poder probarlo
// sin invitado.
class GuestDtSampler {
public:
    using ReadPrev = std::function<uint64_t()>;
    using ReadDt = std::function<uint32_t()>;
    GuestDtSampler(ReadPrev read_prev, ReadDt read_dt)
        : read_prev_(std::move(read_prev)), read_dt_(std::move(read_dt)) {}

    // Una lectura. El invitado escribe el dt ANTES que el contador previo: por eso
    // se lee primero el contador y después el dt, y el cambio se detecta en la
    // muestra siguiente, cuando el dt leído ya es el de la espera terminada.
    void sample();

    const GuestDtResult& result() const { return result_; }

private:
    ReadPrev read_prev_;
    ReadDt read_dt_;
    uint64_t last_prev_ = 0;
    bool have_prev_ = false;
    GuestDtResult result_;
};

// Muestreador con hilo: muestrea cada 1 ms y acumula.
class GuestDtProfile {
public:
    GuestDtProfile(GuestDtSampler::ReadPrev read_prev, GuestDtSampler::ReadDt read_dt)
        : sampler_(std::move(read_prev), std::move(read_dt)) {}
    ~GuestDtProfile() { (void)stop(); }

    bool start();
    bool running() const { return running_; }
    GuestDtResult stop();

private:
    void loop();

    GuestDtSampler sampler_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    bool running_ = false;
};

} // namespace hw::runtime
