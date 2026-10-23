// src/d3d/consumer_phase.h — HW-073 (perf-host) SPEC-054 CA4.
//
// Reporte por ventana del hilo consumer D3D (HW-053, core 1):
//  - TIEMPO ocupado armando/reproduciendo el frame vs esperando un anillo en
//    `wait_take`. Distingue un consumer que satura su core (límite del fps, y
//    riesgo de hambrear a los hilos de audio co-ubicados) de uno ocioso (el
//    límite está en el guest).
//  - TRABAJO por frame: draws y cambios de estado (State/Tss/Texture) leídos del
//    ring en el recorrido que ya se hace para contar uploads. Es la carga que el
//    consumer ejecuta por frame y la base para medir redundancia (HW-040/065).
//
// Unidades: los conteos por frame se emiten en milésimas (× 1000), como el resto
// de la familia `*_milli` del `fps-window`: `tex_uploads_per_frame_milli` usa
// la misma escala y el mismo denominador (los frames completados de la ventana),
// así que "uploads/frame" y "draws/frame" se comparan sin reescalar. Ver el
// contrato "Unidades" de `hotwheels/specs/054-perf-host-a9.md`.
//
// Lo escribe SÓLO el hilo consumer (ver bridge.cpp): sin atómicos, sin locks.
#pragma once

#include "d3d/d3d.h"

#include <cstdint>
#include <cstdio>

namespace hw::d3d {

struct ConsumerPhases {
    uint64_t busy_us = 0;    // dentro de consume_frame
    uint64_t wait_us = 0;    // bloqueado en FrameQueue::wait_take sin anillo
    uint64_t replay_us = 0;  // dentro de Replayer::play / present_frame
    uint64_t draws = 0;      // comandos Draw del ring
    uint64_t state_ops = 0;  // comandos State/Tss/Texture del ring

    void reset() { *this = ConsumerPhases{}; }

    // Promedios por frame completado + ocupación del hilo sobre la ventana.
    // `window_us` es el ancho real de la ventana de reporte.
    // Nota: `busy_pct` puede superar 1000 (100 %) hasta el busy de UN frame, porque
    // el frame que cae sobre el borde de ventana se cuenta entero en la siguiente.
    int format(char* out, std::size_t cap, uint64_t frames, uint64_t window_us) const {
        const uint64_t per = frames ? 1 : 0;  // 0 frames => promedios 0 (sin división)
        const uint64_t busy = per ? busy_us / frames : 0;
        const uint64_t wait = per ? wait_us / frames : 0;
        const uint64_t replay = per ? replay_us / frames : 0;
        const uint64_t pct = window_us ? busy_us * 1000u / window_us : 0;
        const uint64_t draws_milli = per ? draws * 1000u / frames : 0;
        const uint64_t state_milli = per ? state_ops * 1000u / frames : 0;
        return std::snprintf(out, cap,
            "consumer_busy_us_per_frame=%llu consumer_wait_us_per_frame=%llu "
            "consumer_replay_us_per_frame=%llu consumer_busy_pct_milli=%llu "
            "consumer_draws_per_frame_milli=%llu consumer_state_ops_per_frame_milli=%llu",
            (unsigned long long)busy, (unsigned long long)wait,
            (unsigned long long)replay, (unsigned long long)pct,
            (unsigned long long)draws_milli, (unsigned long long)state_milli);
    }
};

// Clasifica una op del ring. TexUpload NO cuenta acá: ya se emite como
// tex_uploads_per_frame_milli desde el mismo recorrido.
inline void count_op(uint16_t op, ConsumerPhases& p) {
    switch (Op(op)) {
    case Op::Draw: ++p.draws; break;
    case Op::State: case Op::Tss: case Op::Texture: ++p.state_ops; break;
    default: break;
    }
}

} // namespace hw::d3d
