// d3d.h — HW-010: renderer D3D6 de carrera (spec 003).
// Device (semántica COM) → Recorder (ring u32) → Replayer → Backend.
#pragma once
#include "../../../winx86-dx/include/winx86-dx/d3d/device.h"
#include "diag/hw_diag.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hw::d3d {

using winx86::dx::d3d::kOk;
using winx86::dx::d3d::kInvalidParams;
using winx86::dx::d3d::kSceneInScene;
using winx86::dx::d3d::kSceneNotInScene;
using winx86::dx::d3d::TexFormat;
using winx86::dx::d3d::Rect;
using winx86::dx::d3d::TextureSource;
using winx86::dx::d3d::TextureLookup;
using winx86::dx::d3d::RS_TEXTUREMAG;
using winx86::dx::d3d::RS_TEXTUREMIN;
using winx86::dx::d3d::RS_SRCBLEND;
using winx86::dx::d3d::RS_DESTBLEND;
using winx86::dx::d3d::RS_CULLMODE;
using winx86::dx::d3d::RS_ALPHABLENDENABLE;
using winx86::dx::d3d::RS_COLORKEYENABLE;
using winx86::dx::d3d::kFvfXyzrhwTex1;

enum class Op : uint16_t { Begin = 1, Viewport, Clear, State, Tss, Texture, Draw, End, Present,
                           TexUpload, TexFree };
// ---------------- ring ----------------
class Ring {
public:
    void put(Op op, const uint32_t* payload, uint32_t n);
    void prepend(const Ring& prefix);
    // Append once and return the writable payload in the ring. The pointer is
    // valid only until the next mutation; used by interp60 to patch XY in place.
    uint32_t* put_patchable(Op op, const uint32_t* payload, uint32_t n);
    void put(Op op, std::initializer_list<uint32_t> p) { put(op, p.begin(), uint32_t(p.size())); }
    const std::vector<uint32_t>& words() const { return w_; }
    void clear() { w_.clear(); }
    // Short commands use [n:16|op:16]; n=0xffff escapes to a full u32 length.
    // On success cursor advances past the payload. False means EOF or truncation.
    static bool next(const std::vector<uint32_t>& words, size_t& cursor,
                     uint16_t& op, const uint32_t*& payload, uint32_t& n);
    // Recorre los comandos: fn(op, payload, n). Ops desconocidos se pasan igual (el lector los ignora).
    void for_each(const std::function<void(uint16_t, const uint32_t*, uint32_t)>& fn) const;
private:
    std::vector<uint32_t> w_;
};

// ---------------- backend ----------------
struct Vertex { float x, y, z, w, u, v; };   // clip space + uv
struct DrawCall {
    uint32_t prim = 5;                // D3DPRIMITIVETYPE
    std::vector<Vertex> verts;
    uint32_t tex = 0;                 // 0 = sin textura
    bool blend = false;
    uint32_t src = 2, dst = 1;        // D3DBLEND
    bool alpha_test = false;          // colorkey
    bool linear = false;
    Rect scissor;
};
enum class TextureFilter { Game, Point, Linear };
constexpr bool effective_linear(TextureFilter policy, bool game_linear, bool keyed_alpha_test = false) {
    // Binary alpha test sees post-filter alpha; partial edge alpha passes GREATER 0 and exposes RGB halos.
    if (keyed_alpha_test) return false;
    return policy == TextureFilter::Linear || (policy == TextureFilter::Game && game_linear);
}
struct TextureData {
    uint32_t id = 0, width = 0, height = 0;
    TexFormat format = TexFormat::ARGB1555;
    std::vector<uint8_t> native;      // filas empaquetadas (pitch = width * bpp)
    std::vector<uint32_t> argb;       // conversión ARGB8888 (colorkey ya aplicado: α=0)
    std::vector<uint32_t> palette;    // PAL8: 256 entradas ARGB originales
    bool keyed = false;
    uint32_t key_low = 0, key_high = 0;
};
struct Backend {
    virtual ~Backend() = default;
    virtual void begin_frame(uint32_t w, uint32_t h) = 0;
    virtual void upload_texture(const TextureData& t) = 0;
    virtual void free_texture(uint32_t id) = 0;
    virtual void clear(const Rect& r, uint32_t argb) = 0;
    virtual void draw(const DrawCall& d) = 0;
    virtual void end_frame() {}
    virtual void present() = 0;
    virtual uint64_t texture_upload_count() const { return 0; }
    // SPEC-052: ventana de diagnóstico del backend (nullptr = sin telemetría). El Replayer la pide
    // perezoso (en el primer play), no en el constructor: si el backend la crea después, no queda
    // nullptr para siempre.
    virtual diag::DiagWindow* diag_window() { return nullptr; }
};

// Decodifica el ring y alimenta al backend.
class Replayer {
public:
    explicit Replayer(Backend& b, bool texture_cache = true);
    void play(const Ring& ring);
    // SPEC-052 HW_DIAG_TEXDEFER: difiere el free_texture al backend N presents (0 = inmediato, como el API).
    void set_texdefer(int frames) { texdefer_ = frames > 0 ? frames : 0; tex_life_.set_defer(texdefer_); }
    // SPEC-062: draws consecutivos con el mismo estado salen en un solo DrawCall (lista). HW_D3D_BATCH=0 lo apaga.
    void set_batching(bool on) { batching_ = on; }
    uint64_t draws_guest() const { return draws_guest_; }
    uint64_t draws_emitted() const { return draws_emitted_; }
    static Vertex to_clip(float x, float y, float u, float v, uint32_t w, uint32_t h);
    static std::vector<uint32_t> to_argb(TexFormat f, const uint8_t* px, uint32_t w, uint32_t h,
                                         uint32_t pitch, const uint32_t* pal, bool key,
                                         uint32_t lo, uint32_t hi);
private:
    // La ventana del backend se resuelve en el primer uso (ver P2#4 de la review sonnet-rev).
    diag::DiagWindow* diag_window();
    Backend& b_;
    void submit(DrawCall&& d);
    void flush();
    bool batching_ = true;
    bool batch_open_ = false;
    DrawCall batch_;
    uint64_t draws_guest_ = 0, draws_emitted_ = 0;
    bool texture_cache_ = true;
    diag::DiagWindow* diag_ = nullptr;   // ventana del backend (siempre-on)
    bool ringstat_ = false;              // muestreo 1/4 de ring_* en play()
    uint32_t diag_scenes_ = 0;
    int texdefer_ = 0;
    diag::TexLifetime tex_life_;
    std::map<uint32_t, uint64_t> texture_hashes_;
    uint64_t texture_uploads_ = 0, texture_cache_hits_ = 0;
    uint32_t tw_ = 0, th_ = 0, tex_ = 0;
    Rect vp_;
    std::map<uint32_t, uint32_t> rs_;
    std::map<uint32_t, bool> keyed_;
public:
    uint64_t texture_uploads() const { return texture_uploads_; }
    uint64_t texture_cache_hits() const { return texture_cache_hits_; }
};

// Ring adapter + public-call compatibility facade; D3D6 state semantics live in winx86-dx.
class Device {
public:
    Device(Ring& ring, uint32_t target_w, uint32_t target_h, TextureLookup lookup);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;
    void set_ring(Ring& ring);
    uint32_t begin_scene();
    uint32_t end_scene();
    uint32_t set_render_state(uint32_t state, uint32_t value);
    uint32_t set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value);
    uint32_t set_texture(uint32_t stage, uint32_t handle);     // handle 0 = ninguna
    uint32_t draw_primitive(uint32_t prim, uint32_t fvf, const float* verts, uint32_t count,
                            uint32_t flags);
    // Viewports
    uint32_t create_viewport();
    uint32_t set_viewport2(uint32_t vp, int32_t x, int32_t y, int32_t w, int32_t h);
    uint32_t set_current_viewport(uint32_t vp);
    uint32_t viewport_clear(uint32_t vp, uint32_t count, const Rect* rects, uint32_t flags);
    void present();
    void forget_texture(uint32_t handle);
    // Formatos de EnumTextureFormats, en orden de anuncio.
    static std::vector<TexFormat> texture_formats();
    std::function<void(const std::string&)> log;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// ---------------- rasterizador de referencia ----------------
class RefRaster final : public Backend {
public:
    void begin_frame(uint32_t w, uint32_t h) override;
    void upload_texture(const TextureData& t) override { ++uploads; tex_[t.id] = t; }
    void free_texture(uint32_t id) override { tex_.erase(id); }
    void clear(const Rect& r, uint32_t argb) override;
    void draw(const DrawCall& d) override;
    void present() override { ++presents; }
    uint64_t texture_upload_count() const override { return uploads; }
    uint32_t pixel(uint32_t x, uint32_t y) const { return fb_[size_t(y) * w_ + x]; }
    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
    uint64_t hash() const;                       // FNV-1a del framebuffer
    bool write_png(const std::string& path) const;
    unsigned uploads = 0, presents = 0;
private:
    uint32_t sample(const TextureData& t, float u, float v, bool linear) const;
    void triangle(const Vertex& a, const Vertex& b, const Vertex& c, const DrawCall& d);
    uint32_t w_ = 0, h_ = 0;
    std::vector<uint32_t> fb_;
    std::map<uint32_t, TextureData> tex_;
};

// Backend vitaGL (src/d3d/vitagl_backend.cpp, sólo Vita): dibuja al framebuffer de
// pantalla con pillarbox; requiere vglInit previo.
std::unique_ptr<Backend> make_vitagl_backend(int screen_w = 960, int screen_h = 544,
                                             bool native_scale = true, bool stretch = false,
                                             TextureFilter filter = TextureFilter::Game);

} // namespace hw::d3d
