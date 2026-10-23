#pragma once

#include <cstdint>

namespace hw::d3d {

enum GlStateChange : uint32_t {
    GlProgram       = 1u << 0,
    GlTexture0      = 1u << 1,
    GlFilter0       = 1u << 2,
    GlTexture0Enable= 1u << 3,
    GlTexture1Enable= 1u << 4,
    GlTexture1      = 1u << 5,
    GlBlend         = 1u << 6,
    GlAlphaTest     = 1u << 7,
};

struct GlDrawState {
    uint32_t program = 0;
    uint32_t texture0 = 0;
    uint32_t filter0 = 0;
    bool filter_enabled = true;
    bool palette_enabled = false;
    uint32_t texture1 = 0;
    bool blend = false;
    uint32_t blend_src = 0;
    uint32_t blend_dst = 0;
    bool alpha_test = false;
};

class GlVertexAttribEnableCache {
public:
    uint32_t request(uint32_t requested) {
        const uint32_t missing = requested & ~enabled_;
        enabled_ |= requested;
        return missing;
    }
    void reset() { enabled_ = 0; }
private:
    uint32_t enabled_ = 0;
};

// Tracks only D3D draw state; callers reset it when another renderer can touch GL.
class GlStateCache {
public:
    uint32_t update(const GlDrawState& s) {
        uint32_t changed = 0;
        if (!valid_ || program_ != s.program) changed |= GlProgram;
        if (!valid_ || texture0_ != s.texture0) changed |= GlTexture0;
        if (!valid_ || (s.filter_enabled &&
            (texture0_ != s.texture0 || !filter_valid_ || filter0_ != s.filter0)))
            changed |= GlFilter0;
        if (!valid_ || !texture0_enabled_) changed |= GlTexture0Enable;
        if (s.palette_enabled) {
            if (!valid_ || !texture1_enabled_) changed |= GlTexture1Enable;
            if (!valid_ || texture1_ != s.texture1) changed |= GlTexture1;
        }
        if (!valid_ || blend_ != s.blend ||
            (s.blend && (blend_src_ != s.blend_src || blend_dst_ != s.blend_dst)))
            changed |= GlBlend;
        if (!valid_ || alpha_test_ != s.alpha_test) changed |= GlAlphaTest;

        valid_ = true;
        program_ = s.program;
        texture0_ = s.texture0;
        if (s.filter_enabled) { filter0_ = s.filter0; filter_valid_ = true; }
        texture0_enabled_ = true;
        if (s.palette_enabled) {
            texture1_enabled_ = true;
            texture1_ = s.texture1;
        }
        blend_ = s.blend;
        blend_src_ = s.blend_src;
        blend_dst_ = s.blend_dst;
        alpha_test_ = s.alpha_test;
        return changed;
    }

    void reset() { *this = GlStateCache{}; }

    void invalidate_texture(uint32_t) { reset(); }

private:
    bool valid_ = false;
    uint32_t program_ = 0, texture0_ = 0, filter0_ = 0, texture1_ = 0;
    uint32_t blend_src_ = 0, blend_dst_ = 0;
    bool texture0_enabled_ = false, texture1_enabled_ = false, filter_valid_ = false;
    bool blend_ = false, alpha_test_ = false;
};

} // namespace hw::d3d
