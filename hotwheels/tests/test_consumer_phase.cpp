// HW-073 (perf-host) SPEC-054 CA4: reporte por ventana del consumer D3D.
//
// Tiempo (ocupado vs esperando) y trabajo por frame (draws / cambios de estado),
// leídos del ring en el mismo recorrido que ya cuenta uploads. Un consumer al
// 100 % de un core lo limita; los conteos por frame son la carga que ejecuta.
// El conteo de cambios de estado es la base para medir redundancia (HW-040/065).
//
// Los conteos por frame se verifican en milésimas (× 1000): la escala de la
// familia `*_milli` del `fps-window` (ver contrato "Unidades" de SPEC-054).
#include "d3d/consumer_phase.h"

#include <cassert>
#include <cstring>
#include <string>

static std::string fmt(const hw::d3d::ConsumerPhases& p, uint64_t frames, uint64_t window_us) {
    char b[320];
    const int n = p.format(b, sizeof b, frames, window_us);
    assert(n > 0 && size_t(n) < sizeof b);
    return std::string(b);
}

int main() {
    // Un frame: 800 us ocupado (600 replay), 200 esperando; ventana 1 s.
    hw::d3d::ConsumerPhases p;
    p.busy_us = 800;
    p.replay_us = 600;
    p.wait_us = 200;
    assert(fmt(p, 1, 1'000'000) ==
        "consumer_busy_us_per_frame=800 consumer_wait_us_per_frame=200 "
        "consumer_replay_us_per_frame=600 consumer_busy_pct_milli=0 "
        "consumer_draws_per_frame_milli=0 consumer_state_ops_per_frame_milli=0");

    // Diez frames: promedios por frame (los conteos en milésimas, como los *_milli del fps-window).
    hw::d3d::ConsumerPhases q;
    q.busy_us = 5'000;    // 10 x 500
    q.replay_us = 3'000;  // 10 x 300
    q.wait_us = 4'000;    // 10 x 400
    q.draws = 9'000;      // 10 x 900 draws/frame -> 900000 milésimas
    q.state_ops = 45'000; // 10 x 4500 ops de estado/frame -> 4500000 milésimas
    assert(fmt(q, 10, 100'000) ==
        "consumer_busy_us_per_frame=500 consumer_wait_us_per_frame=400 "
        "consumer_replay_us_per_frame=300 consumer_busy_pct_milli=50 "
        "consumer_draws_per_frame_milli=900000 consumer_state_ops_per_frame_milli=4500000");

    // Base de la escala: UN draw en UN frame son 1000 milésimas, no 1. Caso
    // inequívoco: si la escala volviera a conteos crudos, acá daría 1.
    hw::d3d::ConsumerPhases s;
    s.draws = 1;
    assert(fmt(s, 1, 1'000'000) ==
        "consumer_busy_us_per_frame=0 consumer_wait_us_per_frame=0 "
        "consumer_replay_us_per_frame=0 consumer_busy_pct_milli=0 "
        "consumer_draws_per_frame_milli=1000 consumer_state_ops_per_frame_milli=0");

    // Sin frames: promedios 0 (sin división); pct sigue midiendo la ventana.
    hw::d3d::ConsumerPhases z;
    z.busy_us = 1234;
    assert(fmt(z, 0, 1'000'000) ==
        "consumer_busy_us_per_frame=0 consumer_wait_us_per_frame=0 "
        "consumer_replay_us_per_frame=0 consumer_busy_pct_milli=1 "
        "consumer_draws_per_frame_milli=0 consumer_state_ops_per_frame_milli=0");

    // Ventana nula: pct 0, sin división.
    hw::d3d::ConsumerPhases w;
    w.busy_us = 999;
    assert(fmt(w, 1, 0).find("consumer_busy_pct_milli=0") != std::string::npos);

    // Clasificación de ops del ring: sólo Draw y los cambios de estado cuentan.
    using hw::d3d::Op;
    hw::d3d::ConsumerPhases c;
    auto count = [&](Op op) { hw::d3d::count_op(uint16_t(op), c); };
    count(Op::Draw);
    count(Op::State);
    count(Op::Tss);
    count(Op::Texture);
    count(Op::Draw);
    count(Op::Begin);
    count(Op::Viewport);
    count(Op::Present);
    count(Op::TexUpload);  // uploads se cuentan aparte (tex_uploads_per_frame_milli)
    assert(c.draws == 2 && c.state_ops == 3);

    c.reset();
    assert(c.draws == 0 && c.state_ops == 0 && c.busy_us == 0 && c.wait_us == 0 && c.replay_us == 0);
    return 0;
}
