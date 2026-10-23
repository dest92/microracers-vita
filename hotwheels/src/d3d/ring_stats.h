// ring_stats.h — HW-067 (SPEC-046): estadística de geometría/recursos de una escena del ring D3D.
// Sólo lee el ring (no altera el replay). Header-only para engancharse en host/Vita sin tocar listas de build.
#pragma once
#include "d3d.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hw::d3d {

struct RingStats {
    uint32_t width = 0, height = 0;          // del último Begin
    uint32_t draws = 0, verts = 0, tris = 0, truncated_draws = 0;
    uint32_t nonfinite = 0;                  // x/y/u/v NaN o inf (geometría/muestreo roto)
    uint32_t nonfinite_zw = 0;               // sólo z/rhw no finitos: diagnóstico (el replay los ignora)
    uint32_t out_of_guard = 0;               // finito pero fuera de [-W,2W]x[-H,2H]
    uint32_t rhw_nonpos = 0, z_out = 0;      // rhw<=0, z fuera de [0,1]
    float x_min = 0, x_max = 0, y_min = 0, y_max = 0;
    float z_min = 0, z_max = 0, rhw_min = 0, rhw_max = 0;
    float max_edge = 0;                      // arista de triángulo más larga (px), sólo vértices finitos
    uint32_t uploads = 0, upload_max_w = 0, upload_max_h = 0;
    uint32_t max_payload = 0;                // n máximo de un comando (header de 16 bits)
    uint16_t max_payload_op = 0;
    uint32_t bad_verts() const { return nonfinite + out_of_guard; }
};

namespace ring_stats_detail {
inline float f(uint32_t u) { float v; std::memcpy(&v, &u, 4); return v; }
inline float edge(const float* a, const float* b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
inline bool finite_xy(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]); }
}

inline RingStats ring_stats(const Ring& ring) {
    using namespace ring_stats_detail;
    RingStats s;
    bool any = false, any_zw = false;
    ring.for_each([&](uint16_t op, const uint32_t* p, uint32_t n) {
        if (n > s.max_payload) { s.max_payload = n; s.max_payload_op = op; }
        switch (Op(op)) {
        case Op::Begin: if (n >= 2) { s.width = p[0]; s.height = p[1]; } break;
        case Op::TexUpload:
            if (n >= 3) {
                ++s.uploads;
                s.upload_max_w = std::max(s.upload_max_w, p[1]);
                s.upload_max_h = std::max(s.upload_max_h, p[2]);
            }
            break;
        case Op::Draw: {
            if (n < 2) break;
            ++s.draws;
            const uint32_t count = p[1];
            if (uint64_t(count) * 6 + 2 > n) { ++s.truncated_draws; break; }
            const float W = float(s.width ? s.width : 640), H = float(s.height ? s.height : 480);
            float prev[2][2] = {{0, 0}, {0, 0}}, first[2] = {0, 0};
            bool prev_ok[2] = {false, false}, first_ok = false;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t* q = p + 2 + i * 6;
                const float v[4] = {f(q[0]), f(q[1]), f(q[2]), f(q[3])};
                ++s.verts;
                const bool fin = std::isfinite(v[0]) && std::isfinite(v[1]) &&
                                 std::isfinite(f(q[4])) && std::isfinite(f(q[5]));
                const bool fin_zw = std::isfinite(v[2]) && std::isfinite(v[3]);
                if (!fin) ++s.nonfinite;
                else {
                    if (!fin_zw) ++s.nonfinite_zw;
                    if (v[0] < -W || v[0] > 2 * W || v[1] < -H || v[1] > 2 * H) ++s.out_of_guard;
                    if (!any) { s.x_min = s.x_max = v[0]; s.y_min = s.y_max = v[1]; any = true; }
                    s.x_min = std::min(s.x_min, v[0]); s.x_max = std::max(s.x_max, v[0]);
                    s.y_min = std::min(s.y_min, v[1]); s.y_max = std::max(s.y_max, v[1]);
                }
                if (fin_zw) {
                    if (v[3] <= 0) ++s.rhw_nonpos;
                    if (v[2] < 0 || v[2] > 1) ++s.z_out;
                    if (!any_zw) { s.z_min = s.z_max = v[2]; s.rhw_min = s.rhw_max = v[3]; any_zw = true; }
                    s.z_min = std::min(s.z_min, v[2]); s.z_max = std::max(s.z_max, v[2]);
                    s.rhw_min = std::min(s.rhw_min, v[3]); s.rhw_max = std::max(s.rhw_max, v[3]);
                }
                const bool ok = finite_xy(v);
                // Triángulo cerrado por este vértice según la topología (4 lista, 5 strip, 6 fan).
                const float* a = nullptr; const float* b = nullptr; bool tri = false, tri_ok = false;
                if (p[0] == 4 && i % 3 == 2) { tri = true; a = prev[0]; b = prev[1]; tri_ok = prev_ok[0] && prev_ok[1]; }
                if (p[0] == 5 && i >= 2) { tri = true; a = prev[0]; b = prev[1]; tri_ok = prev_ok[0] && prev_ok[1]; }
                if (p[0] == 6 && i >= 2) { tri = true; a = first; b = prev[1]; tri_ok = first_ok && prev_ok[1]; }
                if (tri) {
                    ++s.tris;
                    if (tri_ok && ok)
                        s.max_edge = std::max({s.max_edge, edge(a, b), edge(b, v), edge(v, a)});
                }
                if (i == 0) { first[0] = v[0]; first[1] = v[1]; first_ok = ok; }
                prev[0][0] = prev[1][0]; prev[0][1] = prev[1][1]; prev_ok[0] = prev_ok[1];
                prev[1][0] = v[0]; prev[1][1] = v[1]; prev_ok[1] = ok;
            }
            break;
        }
        default: break;
        }
    });
    return s;
}

// Línea "ringstat k=v ..." (sin prefijo de log) para tools/track_oracle.py.
inline void format_ring_stats(const RingStats& s, char* out, size_t cap) {
    std::snprintf(out, cap,
        "ringstat w=%u h=%u draws=%u verts=%u tris=%u trunc=%u nan=%u nan_zw=%u oob=%u rhw_le0=%u z_out=%u "
        "x=%.1f..%.1f y=%.1f..%.1f z=%.4f..%.4f rhw=%.4f..%.4f edge=%.1f uploads=%u up_max=%ux%u max_n=%u max_op=%u",
        s.width, s.height, s.draws, s.verts, s.tris, s.truncated_draws, s.nonfinite, s.nonfinite_zw, s.out_of_guard,
        s.rhw_nonpos, s.z_out, s.x_min, s.x_max, s.y_min, s.y_max, s.z_min, s.z_max, s.rhw_min, s.rhw_max,
        s.max_edge, s.uploads, s.upload_max_w, s.upload_max_h, s.max_payload, unsigned(s.max_payload_op));
}

} // namespace hw::d3d
