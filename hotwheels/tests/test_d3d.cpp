// test_d3d.cpp — criterios R1..R10 de specs/003-d3d6-renderer.md (host).
#include "d3d/d3d.h"
#include "d3d/gl_state_cache.h"
#include "d3d/screen_map.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

using namespace hw::d3d;

static uint16_t c1555(int a, int r, int g, int b) { return uint16_t((a << 15) | (r << 10) | (g << 5) | b); }
static uint32_t argb(int a, int r, int g, int b) { return uint32_t(a) << 24 | r << 16 | g << 8 | b; }
static int ch(uint32_t c, int s) { return int((c >> s) & 0xFF); }
static bool near(uint32_t a, uint32_t b, int tol = 1) {
    for (int s = 0; s < 24; s += 8) if (std::abs(ch(a, s) - ch(b, s)) > tol) return false;
    return true;
}
static bool feq(float a, float b) { return std::fabs(a - b) < 1e-5f; }

struct Tex { TextureSource src; std::vector<uint16_t> px; };
static std::map<uint32_t, Tex> textures;
static TextureLookup lookup = [](uint32_t h, TextureSource& out) {
    auto it = textures.find(h); if (it == textures.end()) return false;
    out = it->second.src; out.pixels = reinterpret_cast<const uint8_t*>(it->second.px.data()); return true; };
static void make_tex(uint32_t h, uint32_t w, uint32_t hh, std::vector<uint16_t> px) {
    Tex t; t.px = std::move(px); t.src.width = w; t.src.height = hh; t.src.pitch = w * 2;
    t.src.format = TexFormat::ARGB1555; t.src.generation = 1; textures[h] = t; }

// Quad STRIP en píxeles: (x0,y0)-(x1,y1) con uv 0..1.
static void quad(Device& d, float x0, float y0, float x1, float y1, uint32_t prim = 5) {
    float v[4][6] = {{x0, y0, 0, 1, 0, 0}, {x1, y0, 0, 1, 1, 0}, {x0, y1, 0, 1, 0, 1}, {x1, y1, 0, 1, 1, 1}};
    if (prim == 6)  // FAN: perímetro TL, TR, BR, BL
        for (int k = 0; k < 6; ++k) std::swap(v[2][k], v[3][k]);
    assert(d.draw_primitive(prim, kFvfXyzrhwTex1, &v[0][0], 4, 0x10) == kOk);
}
static void play(Ring& r, RefRaster& rr) { Replayer(rr).play(r); r.clear(); }

struct CaptureTexture final : Backend {
    TextureData last;
    unsigned uploads = 0;
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const TextureData& t) override { last = t; ++uploads; }
    void free_texture(uint32_t) override {}
    void clear(const Rect&, uint32_t) override {}
    void draw(const DrawCall&) override {}
    void present() override {}
};

int main() {
    // BUG-032: host filtering policy can sharpen small/color-key textures or honor D3D state.
    assert(!effective_linear(TextureFilter::Point, true));
    assert(effective_linear(TextureFilter::Linear, false));
    assert(effective_linear(TextureFilter::Game, true));
    assert(!effective_linear(TextureFilter::Game, false));
    assert(effective_linear(TextureFilter::Linear, true, false));
    assert(!effective_linear(TextureFilter::Linear, true, true));
    assert(!effective_linear(TextureFilter::Game, true, true));
    // HW-040: el cache sólo reporta transiciones GL que requiere el draw.
    {
        GlStateCache cache;
        GlDrawState s;
        s.program = 3; s.texture0 = 11; s.filter0 = 2;
        uint32_t changed = cache.update(s);
        assert((changed & (GlProgram | GlTexture0 | GlFilter0 | GlTexture0Enable |
                           GlBlend | GlAlphaTest)) ==
               (GlProgram | GlTexture0 | GlFilter0 | GlTexture0Enable | GlBlend | GlAlphaTest));
        assert(cache.update(s) == 0);
        s.program = 4;
        assert(cache.update(s) == GlProgram);
        s.program = 3;
        assert(cache.update(s) == GlProgram);
        s.texture0 = 12;
        assert(cache.update(s) == (GlTexture0 | GlFilter0)); // B must get its own sampler params.
        s.texture0 = 11;
        assert(cache.update(s) == (GlTexture0 | GlFilter0)); // A→B→A rebinds and restores params.
        s.filter_enabled = false; s.texture0 = 99;
        assert(cache.update(s) == GlTexture0); // White fallback has fixed sampler params.
        s.filter_enabled = true; s.texture0 = 11;
        assert(cache.update(s) == (GlTexture0 | GlFilter0));
        s.filter0 = 1;
        assert(cache.update(s) == GlFilter0);
        s.palette_enabled = true; s.texture1 = 21;
        assert(cache.update(s) == (GlTexture1Enable | GlTexture1));
        assert(cache.update(s) == 0);
        s.texture1 = 22;
        assert(cache.update(s) == GlTexture1);
        s.palette_enabled = false;
        assert(cache.update(s) == 0);
        s.palette_enabled = true;
        assert(cache.update(s) == 0); // unit 1 keeps its binding while sampling is unused.
        s.blend = true; s.blend_src = 5; s.blend_dst = 6;
        assert(cache.update(s) == GlBlend);
        s.blend_src = 7;
        assert(cache.update(s) == GlBlend);
        s.alpha_test = true;
        assert(cache.update(s) == GlAlphaTest);
        cache.reset();
        assert((cache.update(s) & (GlProgram | GlTexture0 | GlFilter0 | GlTexture0Enable |
                                  GlTexture1Enable | GlTexture1 | GlBlend | GlAlphaTest)) ==
               (GlProgram | GlTexture0 | GlFilter0 | GlTexture0Enable |
                GlTexture1Enable | GlTexture1 | GlBlend | GlAlphaTest));
        cache.invalidate_texture(11);
        assert((cache.update(s) & (GlProgram | GlTexture0 | GlFilter0)) ==
               (GlProgram | GlTexture0 | GlFilter0));
    }
    // HW-065: enabled VAO attribute bits persist across D3D/P8/D3D in this GL context.
    {
        GlVertexAttribEnableCache cache;
        assert(cache.request(0x0fu) == 0x0fu); // cold D3D: enable 0..3
        assert(cache.request(0x0fu) == 0);     // next draw/frame: no redundant enables
        assert(cache.request(0x03u) == 0);     // P8 uses 0/1, changes pointers only
        assert(cache.request(0x0fu) == 0);     // D3D refreshes pointers, mask remains enabled
        cache.reset();
        assert(cache.request(0x0fu) == 0x0fu); // new GL context starts disabled
        GlVertexAttribEnableCache p8_cold;
        assert(p8_cold.request(0x03u) == 0x03u); // P8→D3D cold can repeat 0/1 once
        assert(p8_cold.request(0x0fu) == 0x0cu); // D3D still must enable 2/3
    }
    // BUG-032: pretransformed source pixel centers map into a native viewport.
    {
        const auto fit640 = native_screen_map(640, 480, 960, 544, false);
        assert(fit640.x == 117 && fit640.y == 0 && fit640.w == 725 && fit640.h == 544);
        assert(feq(fit640.first_x, fit640.scale_x * 0.5f));
        assert(feq(fit640.first_y, fit640.scale_y * 0.5f));
        assert(std::fabs(fit640.first_x + 639.f * fit640.scale_x -
                         (fit640.w - fit640.scale_x * 0.5f)) < 1e-3f);
        assert(std::fabs(fit640.first_y + 479.f * fit640.scale_y -
                         (fit640.h - fit640.scale_y * 0.5f)) < 1e-3f);
        const auto fit800 = native_screen_map(800, 600, 960, 544, false);
        assert(fit800.x == 117 && fit800.w == 725 && fit800.h == 544);
        const auto stretch = native_screen_map(640, 480, 960, 544, true);
        assert(stretch.x == 0 && stretch.y == 0 && stretch.w == 960 && stretch.h == 544);

        // New full-output GXP transform is algebraically pixel-equivalent to
        // the legacy glViewport + XYZRHW shader for both guest resolutions.
        for (const auto source : {std::array<uint32_t, 2>{640, 480}, {800, 600}}) {
            const auto map = native_screen_map(source[0], source[1], 960, 544, false);
            const ClipTransform tr = native_clip_transform(map, 960, 544);
            for (float x : {0.f, 17.25f, float(source[0])}) {
                const float old_clip = 2.f * x / source[0] - 1.f + 1.f / source[0];
                const float old_pixel = map.x + (old_clip + 1.f) * map.w * 0.5f;
                const float new_clip = tr.sx * x + tr.tx;
                const float new_pixel = (new_clip + 1.f) * 960.f * 0.5f;
                assert(std::fabs(old_pixel - new_pixel) < 1e-3f);
            }
            for (float y : {0.f, 21.5f, float(source[1])}) {
                const float old_clip = -2.f * y / source[1] + 1.f - 1.f / source[1];
                const float old_pixel = map.y + (1.f - old_clip) * map.h * 0.5f;
                const float new_clip = tr.sy * y + tr.ty;
                const float new_pixel = (1.f - new_clip) * 544.f * 0.5f;
                assert(std::fabs(old_pixel - new_pixel) < 1e-3f);
            }
            const ScreenScissor sc = map_scissor(map, source[0], source[1], 544, 10, 20, 100, 200);
            assert(sc.x == map.x + int64_t(10) * map.w / source[0]);
            assert(sc.w == int64_t(110) * map.w / source[0] - int64_t(10) * map.w / source[0]);
            assert(sc.y == 544 - int64_t(220) * map.h / source[1]);
            assert(sc.h == int64_t(220) * map.h / source[1] - int64_t(20) * map.h / source[1]);
        }
        const ScreenScissor stretch_sc = map_scissor(stretch, 640, 480, 544, 10, 20, 100, 200);
        assert(stretch_sc.x == 15 && stretch_sc.y == 295 && stretch_sc.w == 150);

        // Exercise raster coverage after transforming a synthetic XYZRHW strip.
        const ClipTransform tr = native_clip_transform(fit640, 960, 544);
        DrawCall strip;
        strip.prim = 5;
        const ScreenScissor full_sc = map_scissor(fit640, 640, 480, 544, 0, 0, 640, 480);
        strip.scissor = {full_sc.x, 544 - full_sc.y - full_sc.h, full_sc.w, full_sc.h};
        auto vtx = [&](float x, float y) { return Vertex{x * tr.sx + tr.tx, y * tr.sy + tr.ty, .5f, 1.f, 0.f, 0.f}; };
        strip.verts = {vtx(0, 0), vtx(640, 0), vtx(0, 480), vtx(640, 480)};
        RefRaster mapped;
        mapped.begin_frame(960, 544);
        mapped.clear({0, 0, 960, 544}, 0xFF000000);
        mapped.draw(strip);
        assert(mapped.pixel(480, 272) == 0xFFFFFFFFu);
        assert(mapped.pixel(10, 272) == 0xFF000000u); // fit letterbox
        const ScreenScissor left_sc = map_scissor(fit640, 640, 480, 544, 0, 0, 160, 480);
        strip.scissor = {left_sc.x, 544 - left_sc.y - left_sc.h, left_sc.w, left_sc.h};
        mapped.clear({0, 0, 960, 544}, 0xFF000000);
        mapped.draw(strip);
        assert(mapped.pixel(150, 272) == 0xFFFFFFFFu);
        assert(mapped.pixel(300, 272) == 0xFF000000u); // outside scaled scissor
    }
    // R1 ring
    {
        Ring r; r.put(Op::Viewport, {1, 2, 3, 4}); r.put(Op(77), {9, 9}); r.put(Op::Present, {});
        std::vector<std::pair<uint16_t, std::vector<uint32_t>>> got;
        r.for_each([&](uint16_t op, const uint32_t* p, uint32_t n) { got.push_back({op, {p, p + n}}); });
        assert(got.size() == 3 && got[0].first == 2 && got[0].second == (std::vector<uint32_t>{1, 2, 3, 4}));
        assert(got[1].first == 77 && got[1].second.size() == 2 && got[2].first == 9 && got[2].second.empty());
    }
    // PAL8 shader upload preserves the source palette and D3D color-key range.
    {
        std::array<uint8_t, 4> pixels{0, 1, 2, 3};
        std::array<uint32_t, 256> palette{};
        palette[0] = 0x00000000u; palette[1] = 0xFFFF0000u;
        TextureLookup pal_lookup = [&](uint32_t id, TextureSource& out) {
            if (id != 9) return false;
            out.width = 2; out.height = 2; out.pitch = 2; out.generation = 1;
            out.format = TexFormat::PAL8; out.pixels = pixels.data(); out.palette = palette.data();
            out.has_key = true; out.key_low = 0; out.key_high = 1;
            return true;
        };
        Ring ring; Device d(ring, 2, 2, pal_lookup); CaptureTexture capture;
        d.begin_scene(); d.set_texture(0, 9); d.end_scene(); Replayer(capture).play(ring);
        assert(capture.last.format == TexFormat::PAL8 && capture.last.palette.size() == 256);
        assert(capture.last.keyed && capture.last.key_low == 0 && capture.last.key_high == 1);
        assert(capture.last.palette[0] == 0 && capture.last.palette[1] == 0xFFFF0000u);
    }
    // HW-055 hashes image bytes, format, dimensions and colorkey; same content
    // avoids a backend upload, changes and TexFree invalidate the cache.
    {
        CaptureTexture capture;
        Replayer replay(capture);
        Ring same, changed, freed;
        const uint32_t tex[] = {42, 1, 1, uint32_t(TexFormat::ARGB1555), 2, 0, 0, 0, 0x7fff};
        same.put(Op::TexUpload, tex, 9);
        replay.play(same);
        replay.play(same);
        assert(capture.uploads == 1 && replay.texture_uploads() == 1 && replay.texture_cache_hits() == 1);
        uint32_t tex_changed[9]; std::memcpy(tex_changed, tex, sizeof tex);
        tex_changed[8] = 0x03e0;
        changed.put(Op::TexUpload, tex_changed, 9);
        replay.play(changed);
        assert(capture.uploads == 2 && replay.texture_uploads() == 2);
        freed.put(Op::TexFree, {42});
        replay.play(freed);
        replay.play(same);
        assert(capture.uploads == 3 && replay.texture_uploads() == 3);

        std::array<uint32_t, 265> pal{};
        pal[0] = 43; pal[1] = 1; pal[2] = 1; pal[3] = uint32_t(TexFormat::PAL8);
        pal[4] = 1; pal[5] = 0; pal[6] = 0; pal[7] = 0;
        pal[8] = 0xFFFF0000u; pal[264] = 0;
        Ring pal_same, pal_changed; pal_same.put(Op::TexUpload, pal.data(), pal.size());
        replay.play(pal_same); replay.play(pal_same);
        assert(capture.uploads == 4 && replay.texture_cache_hits() == 2);
        pal[8] = 0xFF00FF00u; pal_changed.put(Op::TexUpload, pal.data(), pal.size());
        replay.play(pal_changed);
        assert(capture.uploads == 5 && replay.texture_uploads() == 5);
    }

    // A truncated PAL8 upload must be rejected before reading its absent palette.
    {
        const std::array<uint32_t, 8> truncated{1, 1, 1, uint32_t(TexFormat::PAL8), 1, 0, 0, 0};
        Ring ring; ring.put(Op::TexUpload, truncated.data(), truncated.size());
        CaptureTexture capture;
        Replayer(capture).play(ring);
        assert(capture.uploads == 0);
    }
    // A complete PAL8 command with an undersized pitch must also be rejected.
    {
        std::vector<uint32_t> payload(8 + 256 + 1, 0);
        payload[0] = 1; payload[1] = 2; payload[2] = 1;
        payload[3] = uint32_t(TexFormat::PAL8); payload[4] = 1;
        Ring ring; ring.put(Op::TexUpload, payload.data(), payload.size());
        CaptureTexture capture;
        Replayer(capture).play(ring);
        assert(capture.uploads == 0);
    }
    // Size arithmetic must reject dimensions that overflow Vita's 32-bit size_t.
    {
        std::vector<uint32_t> payload(8 + 256 + 1, 0);
        payload[0] = 1; payload[1] = 1; payload[2] = UINT32_MAX;
        payload[3] = uint32_t(TexFormat::PAL8); payload[4] = 1;
        Ring ring; ring.put(Op::TexUpload, payload.data(), payload.size());
        CaptureTexture capture;
        Replayer(capture).play(ring);
        assert(capture.uploads == 0);
    }
    // R2 coordenadas
    {
        auto a = Replayer::to_clip(0, 0, 0.25f, 0.5f, 800, 600), b = Replayer::to_clip(799, 599, 0, 0, 800, 600);
        assert(feq(a.x, -1 + 1.f / 800) && feq(a.y, 1 - 1.f / 600) && a.z == 0 && a.w == 1 && a.u == 0.25f && a.v == 0.5f);
        assert(feq(b.x, 1 - 1.f / 800) && feq(b.y, -1 + 1.f / 600));
    }
    const uint16_t R = c1555(1, 31, 0, 0), G = c1555(1, 0, 31, 0), B = c1555(1, 0, 0, 31), W = c1555(1, 31, 31, 31);
    make_tex(1, 2, 2, {R, G, B, W});
    // R3 raster con textura NEAREST
    {
        Ring ring; RefRaster rr; Device d(ring, 64, 48, lookup);
        uint32_t vp = d.create_viewport(); d.set_viewport2(vp, 0, 0, 64, 48); d.set_current_viewport(vp);
        assert(d.begin_scene() == kOk);
        d.set_render_state(RS_TEXTUREMAG, 1); d.set_render_state(RS_TEXTUREMIN, 1);
        assert(d.set_texture_stage_state(0, 1, 2) == kOk);
        assert(d.set_texture(0, 1) == kOk);
        quad(d, -0.5f, -0.5f, 63.5f, 47.5f);
        assert(d.end_scene() == kOk); d.present(); play(ring, rr);
        assert(rr.pixel(5, 5) == argb(255, 255, 0, 0) && rr.pixel(60, 5) == argb(255, 0, 255, 0));
        assert(rr.pixel(5, 40) == argb(255, 0, 0, 255) && rr.pixel(60, 40) == argb(255, 255, 255, 255));
        assert(rr.presents == 1 && rr.uploads == 1);
    }
    // R3b cobertura exacta (regla top-left): quad (1,1)-(5,5) cubre píxeles 1..4, en STRIP y FAN
    for (uint32_t prim : {5u, 6u}) {
        Ring ring; RefRaster rr; Device d(ring, 8, 8, lookup);
        d.begin_scene(); quad(d, 1, 1, 5, 5, prim); d.end_scene(); play(ring, rr);
        for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x)
            assert((rr.pixel(x, y) == 0xFFFFFFFFu) == (x >= 1 && x <= 4 && y >= 1 && y <= 4));
    }
    // R4 blend (combos del juego) sobre fondo gris 128 limpiado con un quad opaco
    make_tex(2, 1, 1, {c1555(1, 16, 16, 16)});                   // gris ~132
    make_tex(3, 1, 1, {c1555(1, 31, 0, 0)});                     // rojo
    make_tex(4, 1, 1, {c1555(0, 0, 31, 0)});                     // verde con α=0
    auto blend_case = [&](uint32_t tex, uint32_t s, uint32_t dfac) {
        Ring ring; RefRaster rr; Device d(ring, 8, 8, lookup);
        uint32_t vp = d.create_viewport(); d.set_viewport2(vp, 0, 0, 8, 8); d.set_current_viewport(vp);
        d.begin_scene(); d.set_render_state(RS_ALPHABLENDENABLE, 0);
        d.set_texture(0, 2); quad(d, -0.5f, -0.5f, 7.5f, 7.5f);
        d.set_render_state(RS_ALPHABLENDENABLE, 1); d.set_render_state(RS_SRCBLEND, s); d.set_render_state(RS_DESTBLEND, dfac);
        d.set_texture(0, tex); quad(d, -0.5f, -0.5f, 7.5f, 7.5f);
        d.end_scene(); play(ring, rr); return rr.pixel(4, 4);
    };
    {
        int g = (16 << 3) | (16 >> 2);                            // 132
        assert(near(blend_case(4, 5, 6), argb(255, g, g, g)));    // SRCALPHA/INVSRCALPHA, α=0 → no cambia
        assert(near(blend_case(3, 5, 6), argb(255, 255, 0, 0)));  // α=1 → reemplaza
        assert(near(blend_case(3, 9, 3), argb(255, std::min(255, 2 * 255 * g / 255), 0, 0), 2)); // DESTCOLOR/SRCCOLOR
        assert(near(blend_case(3, 10, 2), argb(255, std::min(255, 255 * (255 - g) / 255 + g), g, g), 2)); // INVDESTCOLOR/ONE
        assert(near(blend_case(3, 1, 4), argb(255, 0, g, g), 2));  // ZERO/INVSRCCOLOR
    }
    // R5 colorkey
    {
        Tex t; t.px = {c1555(1, 31, 0, 0), c1555(1, 0, 0, 31)}; t.src.width = 2; t.src.height = 1; t.src.pitch = 4;
        t.src.format = TexFormat::RGB565; t.src.generation = 1; t.src.has_key = true;
        t.src.key_low = t.src.key_high = t.px[1]; textures[5] = t;
        for (int enable = 0; enable < 2; ++enable) {
            Ring ring; RefRaster rr; Device d(ring, 8, 4, lookup);
            uint32_t vp = d.create_viewport(); d.set_viewport2(vp, 0, 0, 8, 4); d.set_current_viewport(vp);
            d.begin_scene(); d.viewport_clear(vp, 0, nullptr, 1);
            d.set_render_state(RS_TEXTUREMAG, 1); d.set_render_state(RS_COLORKEYENABLE, uint32_t(enable));
            d.set_texture(0, 5); quad(d, -0.5f, -0.5f, 7.5f, 3.5f);
            d.end_scene(); play(ring, rr);
            assert(rr.pixel(1, 1) != argb(255, 0, 0, 0));          // texel no keyed siempre pinta
            assert((rr.pixel(6, 1) == argb(255, 0, 0, 0)) == bool(enable));
        }
    }
    // R6 viewport: clear y scissor
    {
        Ring ring; RefRaster rr; Device d(ring, 16, 16, lookup);
        uint32_t full = d.create_viewport(); d.set_viewport2(full, 0, 0, 16, 16);
        uint32_t vp = d.create_viewport(); d.set_viewport2(vp, 4, 4, 8, 8);
        d.begin_scene();
        d.set_current_viewport(full); d.set_texture(0, 3); quad(d, -0.5f, -0.5f, 15.5f, 15.5f);   // todo rojo
        d.set_current_viewport(vp); d.viewport_clear(vp, 0, nullptr, 1);                          // centro negro
        d.set_texture(0, 1); quad(d, -0.5f, -0.5f, 15.5f, 15.5f);                                 // recortado
        d.end_scene(); play(ring, rr);
        assert(rr.pixel(1, 1) == argb(255, 255, 0, 0) && rr.pixel(14, 14) == argb(255, 255, 0, 0));
        assert(rr.pixel(11, 4) == argb(255, 0, 255, 0) && rr.pixel(4, 11) == argb(255, 0, 0, 255));  // dentro: textura
        assert(rr.pixel(3, 8) == argb(255, 255, 0, 0) && rr.pixel(12, 8) == argb(255, 255, 0, 0));
    }
    // R7 upload sólo cuando cambia contenido, aunque suba la generación guest.
    {
        Ring ring; RefRaster rr; Device d(ring, 4, 4, lookup);
        d.begin_scene(); d.set_texture(0, 1); d.set_texture(0, 1); d.set_texture(0, 0); d.set_texture(0, 1);
        textures[1].src.generation = 2; d.set_texture(0, 1);
        d.end_scene(); play(ring, rr);
        assert(rr.uploads == 1);
    }
    // R8 validación
    {
        Ring ring; Device d(ring, 4, 4, lookup);
        float v[18] = {};
        assert(d.begin_scene() == kOk && d.begin_scene() == kSceneInScene);
        size_t before = ring.words().size();
        assert(d.draw_primitive(5, 0x144, v, 3, 0) == kInvalidParams && ring.words().size() == before);
        assert(d.draw_primitive(2, kFvfXyzrhwTex1, v, 3, 0) == kInvalidParams);
        assert(Device::texture_formats().front() == TexFormat::ARGB1555);
        assert(d.end_scene() == kOk && d.end_scene() == kSceneNotInScene);
    }
    // R9 LINEAR
    {
        make_tex(6, 2, 1, {c1555(1, 0, 0, 0), c1555(1, 31, 31, 31)});
        Ring ring; RefRaster rr; Device d(ring, 2, 1, lookup);
        uint32_t vp = d.create_viewport(); d.set_viewport2(vp, 0, 0, 2, 1); d.set_current_viewport(vp);
        d.begin_scene(); d.set_render_state(RS_TEXTUREMAG, 2); d.set_render_state(RS_TEXTUREMIN, 2);
        d.set_texture(0, 6);
        // uv constante = 0.5 (borde entre texels) en todo el quad
        float v[4][6] = {{-0.5f, -0.5f, 0, 1, .5f, .5f}, {1.5f, -0.5f, 0, 1, .5f, .5f},
                         {-0.5f, 0.5f, 0, 1, .5f, .5f}, {1.5f, 0.5f, 0, 1, .5f, .5f}};
        d.draw_primitive(5, kFvfXyzrhwTex1, &v[0][0], 4, 0);
        d.end_scene(); play(ring, rr);
        assert(near(rr.pixel(0, 0), argb(255, 128, 128, 128), 2));
    }
    // R10 golden
    {
        Ring ring; RefRaster rr; Device d(ring, 160, 120, lookup);
        uint32_t vp = d.create_viewport(); d.set_viewport2(vp, 0, 0, 160, 120); d.set_current_viewport(vp);
        d.begin_scene(); d.viewport_clear(vp, 0, nullptr, 1);
        d.set_render_state(RS_TEXTUREMAG, 2); d.set_render_state(RS_TEXTUREMIN, 2);
        d.set_texture(0, 1); quad(d, 10, 10, 110, 90);
        d.set_render_state(RS_ALPHABLENDENABLE, 1); d.set_render_state(RS_SRCBLEND, 10); d.set_render_state(RS_DESTBLEND, 2);
        d.set_texture(0, 3);
        float tri[3][6] = {{60, 20, 0, 1, 0, 0}, {150, 60, 0, 1, 1, 0}, {60, 110, 0, 1, 0, 1}};
        d.draw_primitive(4, kFvfXyzrhwTex1, &tri[0][0], 3, 0x10);
        d.set_render_state(RS_ALPHABLENDENABLE, 0); d.set_render_state(RS_COLORKEYENABLE, 1);
        d.set_render_state(RS_TEXTUREMAG, 1); d.set_render_state(RS_TEXTUREMIN, 1);
        d.set_texture(0, 5); quad(d, 20, 70, 140, 115, 6);
        d.end_scene(); d.present(); play(ring, rr);
        assert(rr.write_png("build/d3d_golden.png"));
        std::fprintf(stderr, "golden hash 0x%016llx\n", (unsigned long long)rr.hash());
        assert(rr.hash() == 0x6d85c1e26d7a5b3cull);  // GOLDEN revisado a ojo (build/d3d_golden.png)
    }
    std::puts("test_d3d OK");
    return 0;
}
