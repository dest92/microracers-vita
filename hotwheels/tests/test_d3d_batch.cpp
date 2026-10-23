// SPEC-062: batching de draws consecutivos con estado idéntico. El resultado de RefRaster no cambia.
#include "d3d/d3d.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace hw::d3d;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

struct Counting : Backend {   // RefRaster es final: se envuelve
    RefRaster r;
    uint32_t draws = 0; size_t max_verts = 0;
    void begin_frame(uint32_t w, uint32_t h) override { r.begin_frame(w, h); }
    void upload_texture(const TextureData& t) override { r.upload_texture(t); }
    void free_texture(uint32_t id) override { r.free_texture(id); }
    void clear(const Rect& rc, uint32_t argb) override { r.clear(rc, argb); }
    void draw(const DrawCall& d) override { ++draws; max_verts = std::max(max_verts, d.verts.size()); r.draw(d); }
    void present() override { r.present(); }
    uint64_t hash() const { return r.hash(); }
};
static uint32_t fb(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
static void draw(Ring& r, uint32_t prim, std::vector<float> xyuv) {
    std::vector<uint32_t> p{prim, uint32_t(xyuv.size() / 4)};
    for (size_t i = 0; i < xyuv.size(); i += 4)
        for (float f : {xyuv[i], xyuv[i + 1], 0.5f, 1.f, xyuv[i + 2], xyuv[i + 3]}) p.push_back(fb(f));
    r.put(Op::Draw, p.data(), uint32_t(p.size()));
}
static void upload(Ring& r, uint32_t id, uint16_t color, bool keyed = false) {
    std::vector<uint32_t> p{id, 4, 4, uint32_t(TexFormat::RGB565), 8, keyed ? 1u : 0u, 0, 0};
    for (int i = 0; i < 8; ++i) p.push_back(uint32_t(color) | uint32_t(color) << 16);
    r.put(Op::TexUpload, p.data(), uint32_t(p.size()));
}
static Ring scene() {
    Ring r;
    r.put(Op::Begin, {64, 64});
    r.put(Op::Clear, {0, 0, 64, 64, 0xFF000000u});
    upload(r, 1, 0xF800); upload(r, 2, 0x07E0);
    r.put(Op::Texture, {0, 1});
    draw(r, 5, {0, 0, 0, 0, 20, 0, 1, 0, 0, 20, 0, 1, 20, 20, 1, 1});            // strip (quad)
    draw(r, 6, {30, 0, 0, 0, 50, 0, 1, 0, 50, 20, 1, 1, 30, 20, 0, 1});          // fan (quad)
    draw(r, 4, {0, 30, 0, 0, 20, 30, 1, 0, 0, 50, 0, 1});                         // lista
    draw(r, 5, {40, 30, 0, 0, 60, 30, 1, 0, 40, 50, 0, 1, 60, 50, 1, 1, 50, 60, 1, 1});  // strip impar
    r.put(Op::Texture, {0, 2});
    draw(r, 4, {10, 10, 0, 0, 40, 10, 1, 0, 10, 40, 0, 1});                       // otro estado: encima
    r.put(Op::Texture, {0, 1});
    draw(r, 5, {20, 20, 0, 0, 60, 20, 1, 0, 20, 60, 0, 1, 60, 60, 1, 1});        // vuelve el estado 1
    upload(r, 1, 0x001F);                                                         // re-upload parte el lote
    draw(r, 4, {0, 55, 0, 0, 10, 55, 1, 0, 0, 64, 0, 1});
    r.put(Op::End, {});
    r.put(Op::Present, {});
    return r;
}

int main() {
    Counting a, b;
    Replayer off(a), on(b);
    off.set_batching(false);
    off.play(scene());
    on.play(scene());
    CHECK(a.hash() == b.hash());                          // CA1: mismo resultado
    CHECK(a.draws == 7 && off.draws_emitted() == 7);
    CHECK(on.draws_guest() == 7);
    // CA2: lote {strip, fan, lista, strip impar} | otro estado | estado 1 | upload | lista = 4 emitidos.
    CHECK(b.draws == 4 && on.draws_emitted() == 4);

    {   // CA7: tope 4095 con borde exacto (<=): 1365 listas de 3 = 4095 → 1 envío; 1366 → 2.
        for (int n : {1365, 1366}) {
            Ring r;
            r.put(Op::Begin, {64, 64});
            for (int i = 0; i < n; ++i) draw(r, 4, {0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1});
            Counting c; Replayer rep(c);
            rep.play(r);
            CHECK(c.max_verts <= 4095);
            CHECK(c.draws == (n == 1365 ? 1u : 2u));
        }
    }
    {   // CA7: Begin y End cortan el lote.
        for (Op cut : {Op::Begin, Op::End}) {
            Ring r;
            r.put(Op::Begin, {64, 64});
            draw(r, 4, {0, 0, 0, 0, 9, 0, 1, 0, 0, 9, 0, 1});
            if (cut == Op::Begin) r.put(Op::Begin, {64, 64}); else r.put(Op::End, {});
            draw(r, 4, {10, 10, 0, 0, 19, 10, 1, 0, 10, 19, 0, 1});
            Counting c; Replayer rep(c);
            rep.play(r);
            CHECK(c.draws == 2);
        }
    }
    {   // review-e6749fb P2: un par de draws que difiere sólo en un campo de la clave no se fusiona.
        struct Case { const char* name; void (*between)(Ring&); };
        const Case cases[] = {
            {"blend", [](Ring& r) { r.put(Op::State, {RS_ALPHABLENDENABLE, 1}); }},
            {"src", [](Ring& r) { r.put(Op::State, {RS_SRCBLEND, 5}); }},
            {"dst", [](Ring& r) { r.put(Op::State, {RS_DESTBLEND, 6}); }},
            {"colorkey", [](Ring& r) { r.put(Op::State, {RS_COLORKEYENABLE, 1}); }},
            {"filtro", [](Ring& r) { r.put(Op::State, {RS_TEXTUREMAG, 2}); }},
            {"scissor.x", [](Ring& r) { r.put(Op::Viewport, {1, 0, 64, 64}); }},
            {"scissor.y", [](Ring& r) { r.put(Op::Viewport, {0, 1, 64, 64}); }},
            {"scissor.w", [](Ring& r) { r.put(Op::Viewport, {0, 0, 63, 64}); }},
            {"scissor.h", [](Ring& r) { r.put(Op::Viewport, {0, 0, 64, 63}); }},
            {"clear", [](Ring& r) { r.put(Op::Clear, {0, 0, 8, 8, 0xFF0000FFu}); }},
            {"texfree", [](Ring& r) { r.put(Op::TexFree, {2}); }},
        };
        for (const Case& c : cases) {
            Ring r;
            r.put(Op::Begin, {64, 64});
            upload(r, 1, 0x7BEF, true); upload(r, 2, 0x07E0);
            r.put(Op::Texture, {0, 1});
            draw(r, 5, {0, 0, 0, 0, 30, 0, 1, 0, 0, 30, 0, 1, 30, 30, 1, 1});
            c.between(r);
            draw(r, 5, {10, 10, 0, 0, 50, 10, 1, 0, 10, 50, 0, 1, 50, 50, 1, 1});
            r.put(Op::Present, {});
            Counting x, y; Replayer off(x), on(y);
            off.set_batching(false);
            off.play(r); on.play(r);
            if (y.draws != 2 || x.hash() != y.hash()) { std::printf("FAIL clave %s: emitidos=%u\n", c.name, y.draws); ++fails; }
        }
    }
    {   // El lote no cruza el fin de play(): un ring que termina en Draw se emite antes del siguiente.
        Ring a, b;
        a.put(Op::Begin, {64, 64});
        draw(a, 4, {0, 0, 0, 0, 9, 0, 1, 0, 0, 9, 0, 1});
        draw(b, 4, {10, 10, 0, 0, 19, 10, 1, 0, 10, 19, 0, 1});
        Counting c; Replayer rep(c);
        rep.play(a);
        CHECK(c.draws == 1);
        rep.play(b);
        CHECK(c.draws == 2);
    }
    std::printf(fails ? "test_d3d_batch: %d FALLOS\n" : "test_d3d_batch: OK\n", fails);
    return fails ? 1 : 0;
}
