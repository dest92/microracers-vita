#include "diag/image_ring.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; return 1; } } while (0)

int main() {
    using namespace hw::d3d;
    Ring in, point, linear;
    in.put(Op::Begin, {8, 8});
    in.put(Op::State, {RS_TEXTUREMAG, 1});
    in.put(Op::State, {RS_COLORKEYENABLE, 0});
    in.put(Op::Texture, {0, 7});
    in.put(Op::Draw, {4, 3, 0x3f800000, 0x3f800000, 0, 0, 0, 0,
        0x40000000, 0x3f800000, 0, 0, 0x3f800000, 0,
        0x3f800000, 0x40000000, 0, 0, 0, 0x3f800000});

    hw::diag::ImageRingFilter point_filter(TextureFilter::Point);
    hw::diag::ImageRingFilter linear_filter(TextureFilter::Linear);
    CHECK(point_filter.rewrite(in, point));
    CHECK(linear_filter.rewrite(in, linear));
    auto last_sampler = [](const Ring& ring) {
        uint32_t result = 0;
        ring.for_each([&](uint16_t op, const uint32_t* p, uint32_t n) {
            if (op == uint16_t(Op::State) && n >= 2 && p[0] == RS_TEXTUREMAG) result = p[1];
        });
        return result;
    };
    CHECK(last_sampler(point) == 1);
    CHECK(last_sampler(linear) == 2);

    // Keyed alpha test is a semantic exception: bilinear interpolation would
    // turn a binary color key into partial alpha, so both policies force point.
    Ring keyed;
    keyed.put(Op::TexUpload, {7, 1, 1, uint32_t(TexFormat::ARGB1555), 2, 1, 0, 0, 0});
    keyed.put(Op::State, {RS_COLORKEYENABLE, 1});
    keyed.put(Op::Texture, {0, 7});
    keyed.put(Op::Draw, {4, 3, 0x3f800000, 0x3f800000, 0, 0, 0, 0,
        0x40000000, 0x3f800000, 0, 0, 0x3f800000, 0,
        0x3f800000, 0x40000000, 0, 0, 0, 0x3f800000});
    Ring kp, kl;
    hw::diag::ImageRingFilter pf(TextureFilter::Point), lf(TextureFilter::Linear);
    CHECK(pf.rewrite(keyed, kp)); CHECK(lf.rewrite(keyed, kl));
    CHECK(kp.words() == kl.words());

    // Un mismo stream de comandos y textura debe reproducirse con ambos filtros;
    // el cambio esperado se limita a los píxeles de muestreo de esa escena.
    Ring textured;
    textured.put(Op::Begin, {32, 32});
    textured.put(Op::Clear, {0, 0, 32, 32, 0xff000000u});
    textured.put(Op::TexUpload, {9, 2, 2, uint32_t(TexFormat::ARGB8888), 8, 0, 0, 0,
                                 0xffffffffu, 0xff000000u, 0xff000000u, 0xffffffffu});
    textured.put(Op::Texture, {0, 9});
    textured.put(Op::Draw, {4, 6,
        0, 0, 0, 0x3f800000, 0, 0,
        0x42000000, 0, 0, 0x3f800000, 0x3f800000, 0,
        0, 0x42000000, 0, 0x3f800000, 0, 0x3f800000,
        0, 0x42000000, 0, 0x3f800000, 0, 0x3f800000,
        0x42000000, 0, 0, 0x3f800000, 0x3f800000, 0,
        0x42000000, 0x42000000, 0, 0x3f800000, 0x3f800000, 0x3f800000});
    auto render_hash = [&](TextureFilter filter) {
        RefRaster raster;
        Replayer player(raster, false);
        Ring changed;
        hw::diag::ImageRingFilter rewrite(filter);
        if (!rewrite.rewrite(textured, changed)) return uint64_t(0);
        player.play(changed);
        return raster.hash();
    };
    const auto point_hash = render_hash(TextureFilter::Point);
    const auto linear_hash = render_hash(TextureFilter::Linear);
    CHECK(point_hash != 0 && linear_hash != 0 && point_hash != linear_hash);
    std::cout << "PASS: image ring filter override / keyed-alpha nearest\n";
    return 0;
}
