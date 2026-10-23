#pragma once
#include "d3d/d3d.h"
#include <array>
#include <cstdint>

namespace ring_scene {
using namespace hw::d3d;
inline std::array<std::array<uint16_t, 16>, 3> texels{};
inline std::array<uint8_t, 16> indices{};
inline std::array<uint32_t, 256> palette{};

inline uint16_t argb1555(bool a, unsigned r, unsigned g, unsigned b) {
    return uint16_t((a ? 0x8000 : 0) | ((r & 31) << 10) | ((g & 31) << 5) | (b & 31));
}
inline void init_textures() {
    for (unsigned i=0; i<16; ++i) {
        texels[0][i] = argb1555(true, (i & 1) ? 31 : 8, (i & 4) ? 31 : 5, (i & 2) ? 31 : 4);
        texels[1][i] = argb1555(true, 31, (i & 1) ? 24 : 4, 2);
        texels[2][i] = (i % 4 == 0) ? 0 : argb1555(true, 2, 30, 8);
        indices[i] = uint8_t((i % 4 == 0) ? 0 : 1 + ((i / 4) % 3));
    }
    palette.fill(0xFF202020u);
    palette[0] = 0x00000000u;
    palette[1] = 0xFFFFC020u;
    palette[2] = 0xFF20D0F0u;
    palette[3] = 0xFFE030B0u;
}
inline bool lookup(uint32_t id, TextureSource& out) {
    if (id < 1 || id > 4) return false;
    out.width = out.height = 4; out.generation = 1;
    if (id == 4) {
        out.pitch = 4; out.format = TexFormat::PAL8;
        out.pixels = indices.data(); out.palette = palette.data();
        out.has_key = true; out.key_low = out.key_high = 0;
    } else {
        out.pitch = 8; out.format = TexFormat::ARGB1555;
        out.pixels = reinterpret_cast<const uint8_t*>(texels[id - 1].data());
        out.has_key = id == 3; out.key_low = out.key_high = 0;
    }
    return true;
}
inline void quad(Device& d, float x0, float y0, float x1, float y1, float uv = 1.f) {
    float v[4][6] = {{x0,y0,0,1,0,0},{x1,y0,0,1,uv,0},
                     {x0,y1,0,1,0,uv},{x1,y1,0,1,uv,uv}};
    d.draw_primitive(5, kFvfXyzrhwTex1, &v[0][0], 4, 0x10);
}
inline void build(Device& d) {
    auto vp=d.create_viewport(); d.set_viewport2(vp,0,0,160,120); d.set_current_viewport(vp);
    d.begin_scene(); d.viewport_clear(vp,0,nullptr,1);
    d.set_render_state(RS_TEXTUREMAG,2); d.set_render_state(RS_TEXTUREMIN,2);
    d.set_texture(0,1); quad(d,10,8,105,58);
    d.set_render_state(RS_ALPHABLENDENABLE,1); d.set_render_state(RS_SRCBLEND,10); d.set_render_state(RS_DESTBLEND,2);
    d.set_texture(0,2); float tri[3][6]={{55,20,0,1,0,0},{150,55,0,1,1,0},{55,72,0,1,0,1}};
    d.draw_primitive(4,kFvfXyzrhwTex1,&tri[0][0],3,0x10);
    d.set_render_state(RS_ALPHABLENDENABLE,0);
    d.set_render_state(RS_TEXTUREMAG,1); d.set_render_state(RS_TEXTUREMIN,1);
    // Non-black underlays make colorkey holes observable in Vita3K captures.
    d.set_texture(0,1); quad(d,12,60,82,112); quad(d,87,67,151,113);
    d.set_render_state(RS_TEXTUREMAG,2); d.set_render_state(RS_TEXTUREMIN,2);
    d.set_texture(0,4); quad(d,2,70,10,90);
    d.set_render_state(RS_TEXTUREMAG,1); d.set_render_state(RS_TEXTUREMIN,1);
    d.set_render_state(RS_COLORKEYENABLE,1);
    d.set_texture(0,3); quad(d,12,60,82,112,1);
    d.set_texture(0,4); quad(d,87,67,151,113,1);
    d.set_texture(0,0); quad(d,2,95,8,105);
    d.end_scene(); d.present();
}
}
