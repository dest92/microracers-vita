// src/runtime/heartbeat.h — latidos por hilo para el vigía interno (spec 023,
// BUG-035). Sin locks ni heap: lo leen un hilo vigía y, en un cuelgue, nadie
// más; marcar cuesta 3 stores relajados. Punto impar = dentro de una llamada
// que puede bloquear (syscall/mutex); par = fuera.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace hw::hb {

enum class Slot : unsigned { DdrawPresent, Interp, Audio, LogSink, kCount };

struct Beat {
    std::atomic<uint32_t> point{0}, t_ms{0}, count{0};
};

inline Beat g_beats[unsigned(Slot::kCount)];

struct SlotInfo { const char* name; bool periodic; };
inline constexpr SlotInfo kInfo[unsigned(Slot::kCount)] = {
    {"ddraw", false},   // el guest presenta sólo cuando cambia algo
    {"interp", true},   // hilo interp60: cada vblank
    {"audio", true},    // hilo hw_audio: cada grano (~11 ms)
    {"logsink", false}, // flush del LogBuffer a la memory card
};

inline Beat& beat(Slot s) { return g_beats[unsigned(s)]; }

inline void mark(Slot s, uint32_t point, uint32_t now_ms) {
    Beat& b = beat(s);
    b.t_ms.store(now_ms, std::memory_order_relaxed);
    b.point.store(point, std::memory_order_relaxed);
    b.count.fetch_add(1, std::memory_order_relaxed);
}

// Edad en ms; 0 si el latido (de otro core) es posterior a `now`.
inline uint32_t age(const Beat& b, uint32_t now_ms) {
    const int32_t d = int32_t(now_ms - b.t_ms.load(std::memory_order_relaxed));
    return d > 0 ? uint32_t(d) : 0u;
}

inline void reset() {
    for (Beat& b : g_beats) { b.point.store(0); b.t_ms.store(0); b.count.store(0); }
}

inline bool stalled(Slot s, uint32_t now_ms, uint32_t thr_ms) {
    const Beat& b = beat(s);
    if (b.count.load(std::memory_order_relaxed) == 0) return false;
    const bool inside = b.point.load(std::memory_order_relaxed) & 1u;
    if (!kInfo[unsigned(s)].periodic && !inside) return false;
    return age(b, now_ms) > thr_ms;
}

inline unsigned any_stalled(uint32_t now_ms, uint32_t thr_ms) {
    unsigned mask = 0;
    for (unsigned i = 0; i < unsigned(Slot::kCount); ++i)
        if (stalled(Slot(i), now_ms, thr_ms)) mask |= 1u << i;
    return mask;
}

// "hb t=<now> ddraw=<punto>@<edad_ms>#<contador>[*] ..." ; "-" sin latidos.
// Devuelve la longitud escrita (< cap, siempre termina en NUL si cap > 0).
inline size_t format(char* out, size_t cap, uint32_t now_ms, uint32_t thr_ms) {
    if (!cap) return 0;
    size_t n = 0;
    auto put = [&](int w) {
        if (w < 0) return;
        n += size_t(w);
        if (n >= cap) n = cap - 1;
    };
    put(std::snprintf(out, cap, "hb t=%u", now_ms));
    for (unsigned i = 0; i < unsigned(Slot::kCount) && n + 1 < cap; ++i) {
        const Beat& b = g_beats[i];
        const uint32_t c = b.count.load(std::memory_order_relaxed);
        if (!c) { put(std::snprintf(out + n, cap - n, " %s=-", kInfo[i].name)); continue; }
        put(std::snprintf(out + n, cap - n, " %s=%u@%u#%u%s", kInfo[i].name,
                          b.point.load(std::memory_order_relaxed),
                          age(b, now_ms), c,
                          stalled(Slot(i), now_ms, thr_ms) ? "*" : ""));
    }
    out[n] = 0;
    return n;
}

}  // namespace hw::hb
