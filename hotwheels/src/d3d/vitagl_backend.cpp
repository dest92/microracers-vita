// HW-010 VitaGL backend: tracked GXP shaders, with no runtime FFP compilation.
#include "d3d/d3d.h"
#include "d3d/gl_state_cache.h"
#include "d3d/screen_map.h"
#ifdef HW_D3D_GL_HOST
#include <GL/gl.h>
static void vglSwapBuffers(GLboolean) {}
static void glUseProgram(GLuint) {}
static void glEnableVertexAttribArray(GLuint) {}
static void glDisableVertexAttribArray(GLuint) {}
static void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) {}
static void glVertexAttrib1f(GLuint, GLfloat) {}
static void glVertexAttrib4f(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) {}
static void glActiveTexture(GLenum) {}
#else
#include <vitaGL.h>
#include <psp2/kernel/clib.h>
#include "d3d/shaders/d3d6_gxp_blobs.h"
#endif
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <vector>

// Contadores de diagnóstico del pool circular (SPEC-052): sólo existen en el libvitaGL.a con el
// parche de diagnóstico (patches/vitagl-c707ff00-diag.patch). Son weak: en el build normal
// (SDK) quedan sin resolver y la telemetría de pool no aplica.
extern "C" {
extern __attribute__((weak)) uint32_t vgl_diag_circ_wraps;
extern __attribute__((weak)) uint32_t vgl_diag_circ_overrun;
extern __attribute__((weak)) uint32_t vgl_diag_circ_slot;
extern __attribute__((weak)) uint32_t vgl_diag_circ_off;
extern __attribute__((weak)) uint32_t vgl_diag_vgl_alloc_fail;
extern __attribute__((weak)) uint32_t vgl_diag_canary_bad;
extern __attribute__((weak)) uint32_t vgl_diag_canary_enabled;
}

namespace hw::d3d {
namespace {

// SPEC-052: bytes por vértice que el backend sube al pool circular (xy+uv+color+pal).
constexpr uint32_t kVertexStrideBytes = 9 * 4;

GLenum blend_factor(uint32_t f) {
    switch (f) {
    case 1: return GL_ZERO; case 2: return GL_ONE; case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR; case 5: return GL_SRC_ALPHA; case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_ONE;    // DESTALPHA: el target no tiene alfa
    case 8: return GL_ZERO;   // INVDESTALPHA
    case 9: return GL_DST_COLOR; case 10: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_ONE;
    }
}

GLenum mode(uint32_t prim) {
    return prim == 4 ? GL_TRIANGLES : prim == 6 ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP;
}

struct VitaTexture {
    GLuint rgba = 0;
    GLuint index = 0;
    GLuint palette = 0;
    GLuint keyed_palette = 0;
    std::array<GLfloat, 3> key_rgb{};
    bool pal8 = false, keyed = false;
    uint64_t bytes = 0;   // estimación de backing GL (SPEC-052 tex_bytes)
};

struct Program {
    GLuint id = 0;
    GLint scale = -1;
    GLint mode = -1;
    GLint constant = -1;
    GLint key = -1;
};

uint32_t rgb(uint32_t argb) { return argb & 0x00FFFFFFu; }

uint32_t choose_key_sentinel(const std::vector<uint32_t>& palette, uint32_t lo, uint32_t hi) {
    std::vector<uint32_t> used;
    used.reserve(palette.size());
    for (size_t i = 0; i < palette.size(); ++i) {
        if (i >= lo && i <= hi) continue;
        used.push_back(rgb(palette[i]));
    }
    for (uint32_t candidate = 0; candidate <= 0xFFFFFFu; ++candidate)
        if (std::find(used.begin(), used.end(), candidate) == used.end()) return candidate;
    return 0; // 256 palette colors cannot exhaust RGB24.
}

std::vector<uint8_t> palette_rgba(const TextureData& t, bool keyed, uint32_t sentinel) {
    std::vector<uint8_t> out(256 * 4, 0xFF);
    uint32_t lo = std::min(t.key_low, 255u), hi = std::min(t.key_high, 255u);
    if (lo > hi) std::swap(lo, hi);
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t c = i < t.palette.size() ? t.palette[i] : 0xFF000000u;
        if (keyed && t.keyed && i >= lo && i <= hi)
            c = 0xFF000000u | sentinel;
        out[i * 4 + 0] = uint8_t(c >> 16);
        out[i * 4 + 1] = uint8_t(c >> 8);
        out[i * 4 + 2] = uint8_t(c);
        out[i * 4 + 3] = 0xFF; // the PAL GXP controls alpha with its key variant.
    }
    return out;
}

class VitaGLBackend final : public Backend {
public:
    VitaGLBackend(int sw, int sh, bool native_scale, bool stretch, TextureFilter filter)
        : sw_(sw), sh_(sh), native_scale_(native_scale), stretch_(stretch), filter_(filter),
          toggles_(diag::parse_toggles_env()) {
#ifndef HW_D3D_GL_HOST
        // El canario del pool circular sólo corre si el build de vitaGL tiene el parche y el
        // toggle está activo (cuesta un checksum por frame).
        if (&vgl_diag_canary_enabled && toggles_.canary) vgl_diag_canary_enabled = 1;
#endif
    }

    void begin_frame(uint32_t w, uint32_t h) override {
        cache_.reset();
        glActiveTexture(GL_TEXTURE0);
        active_texture_ = 0;
        ensure_programs();
        circ_.frame_begin();
        if (w != w_ || h != h_) {
            w_ = w; h_ = h;
            map_ = native_screen_map(w, h, sw_, sh_, native_scale_ && stretch_);
            vx_ = map_.x; vy_ = map_.y; vw_ = map_.w; vh_ = map_.h;
        }
        glViewport(native_scale_ ? 0 : vx_, native_scale_ ? 0 : vy_,
                   native_scale_ ? sw_ : vw_, native_scale_ ? sh_ : vh_);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
        glEnable(GL_SCISSOR_TEST);
    }

    void upload_texture(const TextureData& t) override {
        free_texture(t.id);
        VitaTexture texture;
        texture.keyed = t.keyed;
        std::vector<uint8_t> keyed_indices;
        std::vector<uint32_t> keyed_palette;
        bool keyed_rgba = false;
        if (t.format != TexFormat::PAL8 && t.keyed &&
            std::any_of(t.argb.begin(), t.argb.end(), [](uint32_t c) { return (c >> 24) == 0; })) {
            std::map<uint32_t, uint8_t> colors;
            keyed_indices.resize(size_t(t.width) * t.height);
            keyed_palette.assign(256, 0xFF000000u);
            uint16_t next = 0;
            bool fits = true;
            for (size_t i = 0; i < keyed_indices.size(); ++i) {
                uint32_t c = t.argb[i];
                if ((c >> 24) == 0) {
                    keyed_indices[i] = 255;
                    continue;
                }
                uint32_t color = rgb(c);
                auto it = colors.find(color);
                if (it == colors.end()) {
                    if (next >= 255) { fits = false; break; }
                    uint8_t index = uint8_t(next++);
                    colors.emplace(color, index);
                    keyed_palette[index] = 0xFF000000u | color;
                    keyed_indices[i] = index;
                } else keyed_indices[i] = it->second;
            }
            keyed_rgba = fits;
        }
        texture.pal8 = t.format == TexFormat::PAL8 || keyed_rgba;
        if (texture.pal8) {
            const std::vector<uint8_t>& indices = keyed_rgba ? keyed_indices : t.native;
            const std::vector<uint32_t>& colors = keyed_rgba ? keyed_palette : t.palette;
            if (indices.size() < size_t(t.width) * t.height) return;
            glGenTextures(1, &texture.rgba);
            glBindTexture(GL_TEXTURE_2D, texture.rgba);
            rgba_.resize(t.argb.size() * 4);
            for (size_t i = 0; i < t.argb.size(); ++i) {
                uint32_t c = t.argb[i];
                rgba_[i * 4 + 0] = uint8_t(c >> 16);
                rgba_[i * 4 + 1] = uint8_t(c >> 8);
                rgba_[i * 4 + 2] = uint8_t(c);
                rgba_[i * 4 + 3] = uint8_t(c >> 24);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(t.width), GLsizei(t.height),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
            set_texture_params(GL_REPEAT, GL_NEAREST);

            glGenTextures(1, &texture.index);
            glBindTexture(GL_TEXTURE_2D, texture.index);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, GLsizei(t.width), GLsizei(t.height),
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, indices.data());
            set_texture_params(GL_REPEAT, GL_NEAREST);

            TextureData palette_texture = t;
            if (keyed_rgba) {
                palette_texture.keyed = true;
                palette_texture.key_low = palette_texture.key_high = 255;
                palette_texture.palette = colors;
            }
            std::vector<uint8_t> rgba = palette_rgba(palette_texture, false, 0);
            glGenTextures(1, &texture.palette);
            glBindTexture(GL_TEXTURE_2D, texture.palette);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba.data());
            set_texture_params(GL_CLAMP_TO_EDGE, GL_NEAREST);

            if (palette_texture.keyed) {
                uint32_t lo = std::min(palette_texture.key_low, 255u), hi = std::min(palette_texture.key_high, 255u);
                if (lo > hi) std::swap(lo, hi);
                uint32_t sentinel = choose_key_sentinel(palette_texture.palette, lo, hi);
                texture.key_rgb = {float((sentinel >> 16) & 255) / 255.f,
                                   float((sentinel >> 8) & 255) / 255.f,
                                   float(sentinel & 255) / 255.f};
                rgba = palette_rgba(palette_texture, true, sentinel);
                glGenTextures(1, &texture.keyed_palette);
                glBindTexture(GL_TEXTURE_2D, texture.keyed_palette);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, rgba.data());
                set_texture_params(GL_CLAMP_TO_EDGE, GL_NEAREST);
            }
        } else {
            glGenTextures(1, &texture.rgba);
            glBindTexture(GL_TEXTURE_2D, texture.rgba);
            rgba_.resize(t.argb.size() * 4);
            for (size_t i = 0; i < t.argb.size(); ++i) {
                uint32_t c = t.argb[i];
                rgba_[i * 4 + 0] = uint8_t(c >> 16);
                rgba_[i * 4 + 1] = uint8_t(c >> 8);
                rgba_[i * 4 + 2] = uint8_t(c);
                rgba_[i * 4 + 3] = uint8_t(c >> 24);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(t.width), GLsizei(t.height),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
            set_texture_params(GL_REPEAT, GL_NEAREST);
        }
        glActiveTexture(GL_TEXTURE0);
        active_texture_ = 0;
        cache_.reset();
        texture.bytes = uint64_t(t.width) * t.height * 4;
        if (texture.pal8) texture.bytes += uint64_t(t.width) * t.height + 256 * 4 +
                                            (texture.keyed_palette ? 256 * 4 : 0);
        diag_.tex_bytes.fetch_add(uint32_t(texture.bytes), std::memory_order_relaxed);
        tex_[t.id] = texture;
        diag_.tex_live.store(uint32_t(tex_.size()), std::memory_order_relaxed);
        ++texture_uploads_;
    }

    void free_texture(uint32_t id) override {
        auto it = tex_.find(id);
        if (it == tex_.end()) return;
        const VitaTexture& t = it->second;
        if (t.rgba) glDeleteTextures(1, &t.rgba);
        if (t.index) glDeleteTextures(1, &t.index);
        if (t.palette) glDeleteTextures(1, &t.palette);
        if (t.keyed_palette) glDeleteTextures(1, &t.keyed_palette);
        diag_.tex_bytes.fetch_sub(uint32_t(t.bytes), std::memory_order_relaxed);
        tex_.erase(it);
        diag_.tex_live.store(uint32_t(tex_.size()), std::memory_order_relaxed);
        cache_.invalidate_texture(id);
    }

    void clear(const Rect& r, uint32_t argb) override {
        scissor(r);
        glClearColor(float(argb >> 16 & 255) / 255.f, float(argb >> 8 & 255) / 255.f,
                     float(argb & 255) / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void draw(const DrawCall& d) override {
        scissor(d.scissor);
        auto it = d.tex ? tex_.find(d.tex) : tex_.end();
        const VitaTexture* texture = it == tex_.end() ? nullptr : &it->second;
        const bool keyed_alpha_test = texture && texture->keyed && d.alpha_test;
        const bool linear = effective_linear(filter_, d.linear, keyed_alpha_test);
        // Linear filtering must blend resolved colors, not interpolate palette indices.
        bool pal8 = texture && texture->pal8 && !linear;
        Program& program = pal8 ? pal_program_ : rgba_program_;
        if (!program.id) return;

        if (!texture) ensure_white_texture();
        const GLuint texture0 = texture ? (pal8 ? texture->index : texture->rgba) : white_texture_;
        const GLuint texture1 = pal8 ? (d.alpha_test && texture->keyed_palette
                                      ? texture->keyed_palette : texture->palette) : 0;
        GlDrawState state;
        state.program = program.id;
        state.texture0 = texture0;
        state.filter0 = linear ? GL_LINEAR : GL_NEAREST;
        state.filter_enabled = texture != nullptr;
        state.palette_enabled = pal8;
        state.texture1 = texture1;
        state.blend = d.blend;
        state.blend_src = d.src;
        state.blend_dst = d.dst;
        state.alpha_test = d.alpha_test && !pal8;
        const uint32_t changed = cache_.update(state);
        if (changed & GlProgram) { glUseProgram(program.id); ++state_calls_; }
        if (w_ && h_) {
            // XYZRHW screen pixels -> clip space, accounting for the half-pixel center.
#ifndef HW_D3D_GL_HOST
            if (native_scale_) {
                const ClipTransform transform = native_clip_transform(map_, sw_, sh_);
                glUniform4f(program.scale, transform.sx, transform.sy, transform.tx, transform.ty);
            } else {
                glUniform4f(program.scale, 2.f / float(w_), -2.f / float(h_),
                            -1.f + 1.f / float(w_), 1.f - 1.f / float(h_));
            }
#endif
        }
        if (pal8) {
#ifndef HW_D3D_GL_HOST
            glUniform4f(program.constant, 1.f, 1.f, 1.f, 1.f);
            glUniform4f(program.mode, 1.f, d.alpha_test ? 1.f : 0.f, 1.f, 0.f);
            glUniform4f(program.key, texture->key_rgb[0], texture->key_rgb[1],
                        texture->key_rgb[2], 0.f);
#endif
        }

        activate_texture(0);
        if (changed & GlTexture0Enable) { glEnable(GL_TEXTURE_2D); ++state_calls_; }
        if (changed & (GlTexture0 | GlFilter0)) {
            if (changed & GlTexture0) { glBindTexture(GL_TEXTURE_2D, texture0); ++state_calls_; }
            if (changed & GlFilter0) {
                GLint gl_filter = linear ? GL_LINEAR : GL_NEAREST;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter); ++state_calls_;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter); ++state_calls_;
            }
        }
        if (pal8) {
            if (changed & (GlTexture1Enable | GlTexture1)) activate_texture(1);
            if (changed & GlTexture1Enable) { glEnable(GL_TEXTURE_2D); ++state_calls_; }
            if (changed & GlTexture1) { glBindTexture(GL_TEXTURE_2D, texture1); ++state_calls_; }
        }

        if (changed & GlBlend) {
            if (d.blend) {
                glEnable(GL_BLEND); ++state_calls_;
                glBlendFunc(blend_factor(d.src), blend_factor(d.dst)); ++state_calls_;
            } else { glDisable(GL_BLEND); ++state_calls_; }
        }
        if (changed & GlAlphaTest) {
            if (state.alpha_test) {
                glEnable(GL_ALPHA_TEST); ++state_calls_;
                glAlphaFunc(GL_GREATER, 0.f); ++state_calls_;
            } else { glDisable(GL_ALPHA_TEST); ++state_calls_; }
        }

        xy_.resize(d.verts.size() * 2);
        uv_.resize(d.verts.size() * 2);
        color_.resize(d.verts.size() * 4);
        pal_.resize(d.verts.size());
        // SPEC-052 CA3: presión de vértices del pool circular. Es una cota inferior: el
        // backend sube xy+uv+color+pal (9 floats) y no cuenta el buffer de índices.
        circ_.add_draw(uint32_t(d.verts.size()), kVertexStrideBytes);
        for (size_t i = 0; i < d.verts.size(); ++i) {
            xy_[i * 2] = (d.verts[i].x + 1.f) * float(w_) * 0.5f - 0.5f;
            xy_[i * 2 + 1] = (1.f - d.verts[i].y) * float(h_) * 0.5f - 0.5f;
            uv_[i * 2] = d.verts[i].u;
            uv_[i * 2 + 1] = d.verts[i].v;
            color_[i * 4 + 0] = 1.f;
            color_[i * 4 + 1] = 1.f;
            color_[i * 4 + 2] = 1.f;
            color_[i * 4 + 3] = 1.f;
            pal_[i] = 0.5f;
        }
        const uint32_t enable_attributes = attrib_cache_.request(0x0Fu);
        for (uint32_t i = 0; i < 4; ++i) {
            if (enable_attributes & (1u << i)) {
                glEnableVertexAttribArray(i);
                ++attrib_enable_calls_;
            }
        }
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy_.data());
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv_.data());
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, color_.data());
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, pal_.data());
        glDrawArrays(mode(d.prim), 0, GLsizei(d.verts.size()));
        ++draws_;
        attrib_enable_baseline_ += 4;
        // Literal state-call count of the pre-cache draw path: program, active
        // unit 0 at entry/exit, unit-0 enable+bind, optional object filters,
        // optional palette's active1/enable/bind/active0, blend and alpha.
        baseline_state_calls_ += 1 + 2 + 1 + 1 + (texture ? 2 : 0) + (pal8 ? 4 : 0) +
                                 (d.blend ? 2 : 1) + (state.alpha_test ? 2 : 1);
        if (active_texture_ != 0) activate_texture(0);
        if (draws_ >= 64000) report_draw_state();
    }

    void end_frame() override {
#ifndef HW_D3D_GL_HOST
        circ_.frame_end();
        diag_.circ_peak.store(circ_.peak(), std::memory_order_relaxed);
        diag_.circ_overrun.store(circ_.overruns(), std::memory_order_relaxed);
        // SPEC-052: muestreo por frame de memoria libre (mínimo desde el arranque) y de errores GL.
        const uint32_t ram = uint32_t(vglMemFree(VGL_MEM_RAM));
        const uint32_t vram = uint32_t(vglMemFree(VGL_MEM_VRAM));
        const uint32_t cur_ram = diag_.vgl_ram_free_min.load(std::memory_order_relaxed);
        const uint32_t cur_vram = diag_.vgl_vram_free_min.load(std::memory_order_relaxed);
        if (!mem_sampled_ || ram < cur_ram) diag_.vgl_ram_free_min.store(ram, std::memory_order_relaxed);
        if (!mem_sampled_ || vram < cur_vram) diag_.vgl_vram_free_min.store(vram, std::memory_order_relaxed);
        mem_sampled_ = true;
        if (glGetError() != GL_NO_ERROR) diag_.gl_err.fetch_add(1, std::memory_order_relaxed);
        diag_.tex_up.store(uint32_t(texture_uploads_), std::memory_order_relaxed);
        if (&vgl_diag_circ_wraps) {          // sólo con el libvitaGL.a de diagnóstico
            diag_.circ_wraps.store(vgl_diag_circ_wraps, std::memory_order_relaxed);
            diag_.circ_overrun.store(vgl_diag_circ_overrun, std::memory_order_relaxed);
            diag_.circ_slot.store(vgl_diag_circ_slot, std::memory_order_relaxed);
            diag_.circ_off.store(vgl_diag_circ_off, std::memory_order_relaxed);
            diag_.vgl_alloc_fail.store(vgl_diag_vgl_alloc_fail, std::memory_order_relaxed);
            diag_.canary_bad.store(vgl_diag_canary_bad, std::memory_order_relaxed);
        }
#endif
        ++scenes_;
        if (scenes_ < 64) return;
        report_draw_state();
    }

    void report_draw_state() {
#ifndef HW_D3D_GL_HOST
        const uint32_t skipped = baseline_state_calls_ > state_calls_
                               ? baseline_state_calls_ - state_calls_ : 0;
        sceClibPrintf("[d3d-vitagl] state scenes=%u draws=%u calls=%u skipped=%u calls_per_1k=%u attrib_enables=%u attrib_enable_saved=%u\n",
                      scenes_, draws_, state_calls_, skipped,
                      draws_ ? state_calls_ * 1000u / draws_ : 0,
                      attrib_enable_calls_, attrib_enable_baseline_ - attrib_enable_calls_);
#endif
        scenes_ = draws_ = state_calls_ = baseline_state_calls_ = 0;
        attrib_enable_calls_ = attrib_enable_baseline_ = 0;
    }

    void present() override {
        vglSwapBuffers(GL_FALSE);
#ifndef HW_D3D_GL_HOST
        // SPEC-052 HW_DIAG_GLFINISH: espera total a la GPU tras cada swap. Sólo diagnóstico:
        // si el glitch desaparece con esto, la causa es una carrera CPU/GPU (no el estado).
        if (toggles_.glfinish) glFinish();
#endif
    }
    uint64_t texture_upload_count() const override { return texture_uploads_; }
    // SPEC-052: ventana de diagnóstico del backend (siempre activa: contadores enteros baratos).
    diag::DiagWindow* diag_window() override { return &diag_; }

private:
    void activate_texture(unsigned unit) {
        if (active_texture_ == int(unit)) return;
        glActiveTexture(unit ? GL_TEXTURE1 : GL_TEXTURE0);
        ++state_calls_;
        active_texture_ = int(unit);
    }

    void ensure_white_texture() {
#ifndef HW_D3D_GL_HOST
        if (white_texture_) return;
        const uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &white_texture_);
        glBindTexture(GL_TEXTURE_2D, white_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        set_texture_params(GL_REPEAT, GL_NEAREST);
        cache_.reset();
#endif
    }

    static void set_texture_params(GLenum wrap, GLint filter) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    }

    Program make_program(GLuint vertex, GLuint fragment, bool pal8) {
#ifdef HW_D3D_GL_HOST
        (void)vertex; (void)fragment; (void)pal8;
        return {};
#else
        GLuint id = glCreateProgram();
        if (!id) return {};
        glAttachShader(id, vertex); glAttachShader(id, fragment);
        glBindAttribLocation(id, 0, "aPos");
        glBindAttribLocation(id, 1, "aTex");
        glBindAttribLocation(id, 2, "aCol");
        glBindAttribLocation(id, 3, "aPal");
        glLinkProgram(id);
        GLint linked = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &linked);
        if (!linked) { glDeleteProgram(id); return {}; }
        Program result;
        result.id = id;
        result.scale = glGetUniformLocation(id, "uScale");
        if (pal8) {
            result.mode = glGetUniformLocation(id, "uMode");
            result.constant = glGetUniformLocation(id, "uConst");
            result.key = glGetUniformLocation(id, "uKey");
            glUseProgram(id);
            GLint page = glGetUniformLocation(id, "uPage");
            GLint palette = glGetUniformLocation(id, "uPalette");
            if (page >= 0) glUniform1i(page, 0);
            if (palette >= 0) glUniform1i(palette, 1);
        } else {
            glUseProgram(id);
            GLint page = glGetUniformLocation(id, "uPage");
            if (page >= 0) glUniform1i(page, 0);
        }
        if (result.scale < 0 || (pal8 && (result.mode < 0 || result.constant < 0 || result.key < 0))) {
            glDeleteProgram(id);
            return {};
        }
        return result;
#endif
    }

    void ensure_programs() {
#ifdef HW_D3D_GL_HOST
        return;
#else
        if (programs_attempted_) return;
        programs_attempted_ = true;
        GLuint vertex = glCreateShader(GL_CG_VERTEX_SHADER_EXT);
        GLuint fpal = glCreateShader(GL_CG_FRAGMENT_SHADER_EXT);
        GLuint frgba = glCreateShader(GL_CG_FRAGMENT_SHADER_EXT);
        if (!vertex || !fpal || !frgba) goto fail;
        vglShaderGxpBinary(1, &vertex, d3d6_v, size_d3d6_v);
        vglShaderGxpBinary(1, &fpal, d3d6_f_pal, size_d3d6_f_pal);
        vglShaderGxpBinary(1, &frgba, d3d6_f_rgba, size_d3d6_f_rgba);
        pal_program_ = make_program(vertex, fpal, true);
        rgba_program_ = make_program(vertex, frgba, false);
        glDeleteShader(vertex); glDeleteShader(fpal); glDeleteShader(frgba);
        if (!pal_program_.id || !rgba_program_.id) {
            if (pal_program_.id) glDeleteProgram(pal_program_.id);
            if (rgba_program_.id) glDeleteProgram(rgba_program_.id);
            pal_program_ = {}; rgba_program_ = {};
            sceClibPrintf("[d3d-vitagl] GXP program link failed\n");
        }
        return;
fail:
        if (vertex) glDeleteShader(vertex);
        if (fpal) glDeleteShader(fpal);
        if (frgba) glDeleteShader(frgba);
        sceClibPrintf("[d3d-vitagl] GXP shader allocation failed\n");
#endif
    }

    void scissor(const Rect& r) {
        if (!w_ || !h_) return;
        const ScreenScissor s = map_scissor(map_, w_, h_, sh_, r.x, r.y, r.w, r.h);
        glScissor(s.x, s.y, s.w, s.h);
    }

    int sw_, sh_, vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;
    bool native_scale_ = true, stretch_ = false;
    TextureFilter filter_ = TextureFilter::Game;
    diag::Toggles toggles_;
    diag::DiagWindow diag_;
    // Pool circular: la porción es el pool dividido por los buffers de display (3 en vitaGL).
    diag::CircWindow circ_{uint32_t(toggles_.circ_mb) * 1024 * 1024, 3};
    bool mem_sampled_ = false;
    NativeScreenMap map_;
    uint32_t w_ = 0, h_ = 0;
    std::map<uint32_t, VitaTexture> tex_;
    std::vector<uint8_t> rgba_;
    uint64_t texture_uploads_ = 0;
    std::vector<float> xy_, uv_, color_, pal_;
    Program pal_program_, rgba_program_;
    GLuint white_texture_ = 0;
    bool programs_attempted_ = false;
    GlStateCache cache_;
    // VAO state persists across frames and the P8 presenter; backend lifetime is context lifetime.
    GlVertexAttribEnableCache attrib_cache_;
    int active_texture_ = 0;
    uint32_t scenes_ = 0, draws_ = 0, state_calls_ = 0, baseline_state_calls_ = 0;
    uint32_t attrib_enable_calls_ = 0, attrib_enable_baseline_ = 0;
};

} // namespace

std::unique_ptr<Backend> make_vitagl_backend(int screen_w, int screen_h,
                                             bool native_scale, bool stretch, TextureFilter filter) {
    return std::make_unique<VitaGLBackend>(screen_w, screen_h, native_scale, stretch, filter);
}

} // namespace hw::d3d
