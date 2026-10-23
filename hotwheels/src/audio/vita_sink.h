// src/audio/vita_sink.h — sink sceAudioOut con hilo dedicado (HW-018, sólo Vita).
#pragma once
#include <cstdlib>

#include "audio/sink.h"

namespace hw::audio {

// Scheduling de los hilos de audio (SPEC-048 Fase 2). El feeder que llama a
// sceAudioOutOutput y el productor self-paced (dsound_bridge) comparten core con
// el consumer D3D (USER_1 por defecto), que en carrera es el hilo más
// demandante: cuando una ráfaga suya pasa el colchón del driver, el feeder llega
// tarde y el hardware repite el último bloque (ruido, no silencio). El diseño
// (perf-plan palanca 7) fija render=USER_1, audio=USER_2; acá se mueven ambos
// hilos de audio a USER_2 y a una prioridad menor (= más alta) que el default
// del consumer (0x10000100). Ojo: el feeder sí bloquea ~un grano en Output, pero
// el productor self-paced mezcla CPU cada vez que lo despiertan, así que la
// prioridad alta puede retrasar a interp60 y al writer de captura (USER_2): sin
// el A/B de consola no se afirma que no compita con ellos.
// HW_SONCPU=1 vuelve a USER_1, para el A/B en consola.
inline constexpr unsigned kAudioCpuMask = 0x00040000u;       // SCE_KERNEL_CPU_MASK_USER_2
// El SDK codifica la prioridad como 0x1000xxxx = RELATIVA al default del
// proceso; el default absoluto es 0xA0=160 y el rango legal 64..191 ("menos es
// más alto"). 0x100000E0 = DEFAULT-32 -> absoluto 0x80=128 (medido en Vita3K:
// el readback devuelve 0x80, no 0x100000E0).
inline constexpr int      kAudioThreadPriority = 0x100000E0;  // relativo: DEFAULT-32
inline constexpr int      kAudioThreadPriorityAbs = 0x80;     // absoluto equivalente

inline unsigned audio_thread_cpu_mask() {
    const char* c = std::getenv("HW_SONCPU");
    return (c && *c == '1') ? 0x00020000u : kAudioCpuMask;
}

// El hilo del sink llama `step(ctx)` en loop; cada `step` debe mezclar un grano
// y llamar write(), que bloquea en sceAudioOutOutput. Así el hilo del guest
// nunca se bloquea (nunca llama a write()).
struct AudioThreadSpec {
    int grain = 512;                // frames por grano (512 @48000 = 10.7 ms)
    void (*step)(void* ctx) = nullptr;
    void* ctx = nullptr;
};

// Crea el sink. En builds no-Vita devuelve nullptr (no hay sceAudioOut).
Sink* make_vita_sink(const AudioThreadSpec& spec);

}  // namespace hw::audio
