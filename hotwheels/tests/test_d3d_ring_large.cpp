#include "d3d/d3d.h"
#include "d3d/frame_queue.h"
#include "fps/interp60.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace hw::d3d;
static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

static void boundaries() {
    for (uint32_t n : {0u, 65534u, 65535u, 65536u, 131080u}) {
        Ring r;
        std::vector<uint32_t> payload(n, 0x12345678u);
        auto* patch = r.put_patchable(Op(77), payload.data(), n);
        if (n) { patch[n - 1] = 0xabcdef01u; payload.back() = patch[n - 1]; }
        r.put(Op::Present, {});
        unsigned calls = 0;
        r.for_each([&](uint16_t op, const uint32_t* p, uint32_t size) {
            if (calls++ == 0) {
                assert(op == 77 && size == n);
                assert(std::vector<uint32_t>(p, p + size) == payload);
            } else assert(op == uint16_t(Op::Present) && size == 0);
        });
        assert(calls == 2);
    }
    // Truncation and hostile lengths never yield a payload or advance cursor.
    for (const auto& words : {std::vector<uint32_t>{0xffff004du},
            std::vector<uint32_t>{0xffff004du, 0xffffffffu, 1},
            std::vector<uint32_t>{0x0002004du, 1}}) {
        size_t cursor = 0;
        uint16_t op = 0;
        uint32_t n = 0;
        const uint32_t* p = nullptr;
        assert(!Ring::next(words, cursor, op, p, n));
        assert(cursor == 0 && p == nullptr && op == 0 && n == 0);
    }
}

static Ring texture_frame(uint32_t width, uint32_t height, TexFormat format) {
    const uint32_t bytes = format == TexFormat::ARGB8888 ? 4 : 2;
    std::vector<uint8_t> pixels(size_t(width) * height * bytes);
    for (size_t i = 0; i < pixels.size(); i += bytes) {
        if (bytes == 4) { const uint32_t green = 0xff00ff00u; std::memcpy(pixels.data() + i, &green, 4); }
        else { pixels[i] = 0xe0; pixels[i + 1] = 0x07; }
    }
    TextureLookup lookup = [&](uint32_t, TextureSource& t) {
        t.width = width; t.height = height; t.pitch = width * bytes;
        t.format = format; t.generation = 1; t.pixels = pixels.data(); return true;
    };
    Ring r;
    Device d(r, 8, 8, lookup);
    assert(d.begin_scene() == kOk);
    assert(d.set_texture(0, 9) == kOk);
    const float quad[] = {0,0,0,1,0,0, 8,0,0,1,1,0, 0,8,0,1,0,1, 8,8,0,1,1,1};
    assert(d.draw_primitive(5, kFvfXyzrhwTex1, quad, 4, 0) == kOk);
    assert(d.end_scene() == kOk);
    d.present();
    return r;
}

static void textures() {
    for (auto f : {TexFormat::ARGB8888, TexFormat::RGB565}) {
        Ring r = texture_frame(f == TexFormat::ARGB8888 ? 256 : 512, 256, f);
        RefRaster raster;
        Replayer(raster).play(r);
        assert(raster.uploads == 1 && raster.presents == 1);
        assert(raster.pixel(3, 3) == 0xff00ff00u);
    }
}

static Ring large_draw(float x) {
    Ring r = texture_frame(256, 256, TexFormat::ARGB8888);
    Ring prefix;
    prefix.put(Op::Begin, {8, 8});
    // More vertices than the old header could represent. Repeated identical
    // vertices keep the geometry finite and the interpolation displacement small.
    std::vector<uint32_t> draw{5, 10923};
    for (unsigned i = 0; i < 10923; ++i)
        draw.insert(draw.end(), {bits(x), bits(2), bits(0), bits(1), bits(0), bits(0)});
    prefix.put(Op::Draw, draw.data(), uint32_t(draw.size()));
    // Copy only resource commands from a real Device-produced frame.
    r.for_each([&](uint16_t op, const uint32_t* p, uint32_t n) {
        if (Op(op) == Op::TexUpload) prefix.put(Op(op), p, n);
    });
    prefix.put(Op::End, {}); prefix.put(Op::Present, {});
    return prefix;
}

static void interp() {
    hw::fps::Interp60 interp;
    Ring a = large_draw(1), b = large_draw(3), out;
    assert(interp.capture(a, 100000, {0}));
    assert(interp.capture(b, 125000, {0}));
    hw::fps::InterpStats stats;
    assert(interp.tick(125000, out, &stats));
    assert(stats.interpolated == 1);
    out.for_each([](uint16_t op, const uint32_t* p, uint32_t n) {
        if (Op(op) == Op::Draw) {
            assert(n == 2 + 10923 * 6 && p[1] == 10923);
            for (unsigned i = 0; i < p[1]; ++i) {
                assert(p[2 + i * 6] == bits(1));
                assert(p[3 + i * 6] == bits(2));
                assert(p[5 + i * 6] == bits(1));
            }
        }
    });
    RefRaster raster;
    Replayer(raster).play(out);
    assert(raster.uploads == 1 && raster.presents == 1);
}

static void carry() {
    FrameQueue queue;
    auto held = queue.begin_write();
    held.ring()->put(Op::Present, {}); assert(held.publish());
    auto lease = queue.take(); assert(lease);
    auto dropped = queue.begin_write();
    *dropped.ring() = texture_frame(256, 256, TexFormat::ARGB8888);
    assert(dropped.publish());
    auto survivor = queue.begin_write();
    survivor.ring()->put(Op::Begin, {8, 8});
    survivor.ring()->put(Op::Draw, {4,3, bits(0),bits(0),0,bits(1),0,0,
        bits(8),bits(0),0,bits(1),0,0, bits(0),bits(8),0,bits(1),0,0});
    survivor.ring()->put(Op::End, {}); survivor.ring()->put(Op::Present, {});
    assert(survivor.publish());
    assert(queue.stats().dropped == 1);
    auto result = queue.take(); assert(result);
    RefRaster raster;
    Replayer(raster).play(*result.ring());
    assert(raster.uploads == 1 && raster.pixel(2, 2) == 0xff00ff00u);
    result.finish(); lease.finish(); queue.close(true);
}

int main(int argc, char** argv) {
    if (argc == 1 || std::strcmp(argv[1], "boundaries") == 0) boundaries();
    if (argc == 1 || std::strcmp(argv[1], "textures") == 0) textures();
    if (argc == 1 || std::strcmp(argv[1], "interp") == 0) interp();
    if (argc == 1 || std::strcmp(argv[1], "carry") == 0) carry();
    std::puts("D3D large ring: PASS");
}
