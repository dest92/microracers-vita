// HW-067 (SPEC-046 CA2): estadística de ring por escena para el oráculo de BUG-047.
#include "d3d/d3d.h"
#include "d3d/ring_stats.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace hw::d3d;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static uint32_t fb(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
static void draw(Ring& r, uint32_t prim, std::initializer_list<float> xyzrhw) {
    std::vector<uint32_t> p{prim, uint32_t(xyzrhw.size() / 4)};
    const float* v = xyzrhw.begin();
    for (size_t i = 0; i < xyzrhw.size(); i += 4) {
        for (int k = 0; k < 4; ++k) p.push_back(fb(v[i + k]));
        p.push_back(fb(0)); p.push_back(fb(0));
    }
    r.put(Op::Draw, p.data(), uint32_t(p.size()));
}

int main() {
    {   // ring vacío
        Ring r;
        RingStats s = ring_stats(r);
        CHECK(s.draws == 0 && s.verts == 0 && s.tris == 0 && s.max_payload == 0 && s.bad_verts() == 0);
    }
    {   // escena sana 640x480 + una lista con un vértice saltado + uno NaN
        Ring r;
        r.put(Op::Begin, {640, 480});
        r.put(Op::Viewport, {0, 0, 640, 480});
        draw(r, 4, {0, 0, 0.5f, 1, 100, 0, 0.5f, 1, 0, 100, 0.5f, 1});            // lista: 1 tri, arista máx 141.4
        draw(r, 5, {10, 10, 0, 1, 20, 10, 0, 1, 10, 20, 0, 1, 20, 20, 0, 1});      // strip: 2 tris
        const float nan = std::numeric_limits<float>::quiet_NaN();
        draw(r, 6, {0, 0, 1.5f, -1, 5000, 0, 0, 1, 0, 10, 0, 1, nan, 0, 0, 1});   // fan: 2 tris; 1 fuera de guarda, 1 no finito, rhw<=0, z>1
        const uint32_t up[8 + 4] = {7, 2, 2, uint32_t(TexFormat::RGB565), 4, 0, 0, 0};
        r.put(Op::TexUpload, up, 12);
        r.put(Op::End, {});
        RingStats s = ring_stats(r);
        CHECK(s.width == 640 && s.height == 480);
        CHECK(s.draws == 3);
        CHECK(s.verts == 3 + 4 + 4);
        CHECK(s.tris == 1 + 2 + 2);
        CHECK(s.nonfinite == 1);
        CHECK(s.out_of_guard == 1);
        CHECK(s.rhw_nonpos == 1);
        CHECK(s.z_out == 1);
        CHECK(s.uploads == 1 && s.upload_max_w == 2 && s.upload_max_h == 2);
        CHECK(s.max_payload == 2 + 4 * 6 && s.max_payload_op == uint16_t(Op::Draw));   // strip/fan > upload(12)
        CHECK(s.x_min == 0 && s.x_max == 5000 && s.y_min == 0 && s.y_max == 100);
        CHECK(s.rhw_min == -1 && s.rhw_max == 1 && s.z_min == 0 && s.z_max == 1.5f);
        CHECK(std::fabs(s.max_edge - 5000.0f) < 0.5f);   // tri del fan (0,0)-(5000,0)-(0,10)
        CHECK(s.bad_verts() == 2);
        CHECK(s.nonfinite_zw == 0);
        char line[512];
        format_ring_stats(s, line, sizeof line);
        CHECK(std::strstr(line, "draws=3") && std::strstr(line, "oob=1") && std::strstr(line, "nan=1"));
    }
    {   // NaN sólo en Z/RHW: diagnóstico, no geometría mala (el replay los ignora)
        Ring r;
        r.put(Op::Begin, {640, 480});
        const float nan = std::numeric_limits<float>::quiet_NaN();
        draw(r, 4, {0, 0, nan, 1, 10, 0, 0, nan, 0, 10, 0, 1});
        RingStats s = ring_stats(r);
        CHECK(s.nonfinite == 0 && s.nonfinite_zw == 2 && s.bad_verts() == 0);
        CHECK(s.tris == 1 && s.max_edge > 14.0f);
    }
    {   // draw con count mayor al payload: no lee fuera, cuenta como truncado
        Ring r;
        r.put(Op::Begin, {640, 480});
        const uint32_t p[2 + 6] = {4, 99, 0, 0, 0, 0, 0, 0};
        r.put(Op::Draw, p, 8);
        RingStats s = ring_stats(r);
        CHECK(s.draws == 1 && s.verts == 0 && s.truncated_draws == 1);
    }
    std::printf(fails ? "test_ring_stats: %d FALLOS\n" : "test_ring_stats: OK\n", fails);
    return fails ? 1 : 0;
}
