// core.cpp — HW-010: Ring, Device (semántica D3D6) y Replayer. Spec 003.
#include "d3d/d3d.h"
#include "d3d/ring_stats.h"
#ifndef HW_D3D_LINK_WINX86_DX
// Compatibility path for build_host.sh/build_vita.sh while their source lists
// are reserved by runtime (HW-051); CMake and direct runners link the target.
#include "../../../winx86-dx/src/d3d/device.cpp"
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace hw::d3d {

void Ring::put(Op op, const uint32_t* p, uint32_t n) {
    (void)put_patchable(op, p, n);
}
void Ring::prepend(const Ring& prefix) {
    if (prefix.w_.empty()) return;
    std::vector<uint32_t> merged;
    merged.reserve(prefix.w_.size() + w_.size());
    merged.insert(merged.end(), prefix.w_.begin(), prefix.w_.end());
    merged.insert(merged.end(), w_.begin(), w_.end());
    w_.swap(merged);
}
uint32_t* Ring::put_patchable(Op op, const uint32_t* p, uint32_t n) {
    w_.push_back(uint32_t(op) | (std::min(n, 0xffffu) << 16));
    if (n >= 0xffffu) w_.push_back(n);
    const size_t payload = w_.size();
    if (n) w_.insert(w_.end(), p, p + n);
    return w_.data() + payload;
}
bool Ring::next(const std::vector<uint32_t>& words, size_t& cursor,
                uint16_t& op, const uint32_t*& payload, uint32_t& n) {
    size_t i = cursor;
    if (i >= words.size()) return false;
    const uint32_t header = words[i++];
    uint32_t length = header >> 16;
    if (length == 0xffffu) {
        if (i == words.size()) return false;
        length = words[i++];
    }
    if (size_t(length) > words.size() - i) return false;
    op = uint16_t(header & 0xffffu);
    payload = words.data() + i;
    n = length;
    cursor = i + length;
    return true;
}
void Ring::for_each(const std::function<void(uint16_t, const uint32_t*, uint32_t)>& fn) const {
    size_t cursor = 0;
    uint16_t op;
    uint32_t n;
    const uint32_t* payload;
    while (next(w_, cursor, op, payload, n)) fn(op, payload, n);
}

namespace {
uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float bitsf(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
uint32_t bpp(TexFormat f) { return f == TexFormat::ARGB8888 ? 4 : f == TexFormat::PAL8 ? 1 : 2; }
uint32_t e5(uint32_t v) { return (v << 3) | (v >> 2); }
uint32_t e6(uint32_t v) { return (v << 2) | (v >> 4); }
uint64_t texture_hash(uint32_t width, uint32_t height, uint32_t pitch, TexFormat format, bool keyed,
                      uint32_t key_low, uint32_t key_high, const uint32_t* palette,
                      size_t palette_size, const uint8_t* native, size_t native_size) {
    uint64_t h = 14695981039346656037ull;
    auto add = [&](const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    };
    add(&width, sizeof width); add(&height, sizeof height); add(&pitch, sizeof pitch);
    add(&format, sizeof format); add(&keyed, sizeof keyed);
    add(&key_low, sizeof key_low); add(&key_high, sizeof key_high);
    if (palette && palette_size) add(palette, palette_size * sizeof(uint32_t));
    if (native && native_size) add(native, native_size);
    return h;
}
} // namespace

// ---------------- Generic D3D Device → Hot Wheels wire Ring adapter ----------------
namespace {
class RingCommandSink final : public winx86::dx::d3d::CommandSink {
public:
    explicit RingCommandSink(Ring& ring) : ring_(&ring) {}
    void set_ring(Ring& ring) { ring_ = &ring; }
    void begin_scene(uint32_t w, uint32_t h) override { ring_->put(Op::Begin, {w, h}); }
    void end_scene() override { ring_->put(Op::End, {}); }
    void set_render_state(uint32_t state, uint32_t value) override { ring_->put(Op::State, {state, value}); }
    void set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value) override {
        ring_->put(Op::Tss, {stage, type, value});
    }
    void set_texture(uint32_t stage, uint32_t handle) override { ring_->put(Op::Texture, {stage, handle}); }
    void upload_texture(const winx86::dx::d3d::TextureUpload& t) override {
        std::vector<uint32_t> payload{t.handle, t.width, t.height, uint32_t(t.format), t.pitch,
                                      t.has_key ? 1u : 0u, t.key_low, t.key_high};
        if (t.format == TexFormat::PAL8) payload.insert(payload.end(), t.palette.begin(), t.palette.end());
        const size_t first_pixel_word = payload.size();
        payload.resize(first_pixel_word + (t.pixels.size() + 3) / 4, 0);
        if (!t.pixels.empty()) std::memcpy(payload.data() + first_pixel_word, t.pixels.data(), t.pixels.size());
        ring_->put(Op::TexUpload, payload.data(), uint32_t(payload.size()));
    }
    void draw_primitive(uint32_t primitive, const std::vector<winx86::dx::d3d::DrawVertex>& vertices) override {
        std::vector<uint32_t> payload{primitive, uint32_t(vertices.size())};
        payload.reserve(2 + vertices.size() * 6);
        for (const auto& v : vertices) {
            payload.push_back(fbits(v.x)); payload.push_back(fbits(v.y)); payload.push_back(fbits(v.z));
            payload.push_back(fbits(v.rhw)); payload.push_back(fbits(v.u)); payload.push_back(fbits(v.v));
        }
        ring_->put(Op::Draw, payload.data(), uint32_t(payload.size()));
    }
    void set_viewport(Rect r) override {
        ring_->put(Op::Viewport, {uint32_t(r.x), uint32_t(r.y), uint32_t(r.w), uint32_t(r.h)});
    }
    void clear(Rect r, uint32_t argb) override {
        ring_->put(Op::Clear, {uint32_t(r.x), uint32_t(r.y), uint32_t(r.w), uint32_t(r.h), argb});
    }
    void present() override { ring_->put(Op::Present, {}); }
    void free_texture(uint32_t handle) override { ring_->put(Op::TexFree, {handle}); }
private:
    Ring* ring_;
};
} // namespace

struct Device::Impl {
    RingCommandSink sink;
    TextureLookup lookup;
    winx86::dx::d3d::Device generic;
    Impl(Ring& ring, uint32_t w, uint32_t h, TextureLookup fn)
        : sink(ring), lookup(std::move(fn)), generic(sink, w, h, lookup) {}
};

Device::Device(Ring& ring, uint32_t w, uint32_t h, TextureLookup lookup)
    : p_(std::make_unique<Impl>(ring, w, h, std::move(lookup))) {}
Device::~Device() = default;
void Device::set_ring(Ring& ring) { p_->sink.set_ring(ring); }
uint32_t Device::begin_scene() { p_->generic.log = log; return p_->generic.begin_scene(); }
uint32_t Device::end_scene() { p_->generic.log = log; return p_->generic.end_scene(); }
uint32_t Device::set_render_state(uint32_t s, uint32_t v) { p_->generic.log = log; return p_->generic.set_render_state(s, v); }
uint32_t Device::set_texture_stage_state(uint32_t s, uint32_t t, uint32_t v) { p_->generic.log = log; return p_->generic.set_texture_stage_state(s, t, v); }
uint32_t Device::set_texture(uint32_t s, uint32_t h) { p_->generic.log = log; return p_->generic.set_texture(s, h); }
uint32_t Device::draw_primitive(uint32_t p, uint32_t f, const float* v, uint32_t n, uint32_t flags) {
    p_->generic.log = log; return p_->generic.draw_primitive(p, f, v, n, flags);
}
uint32_t Device::create_viewport() { p_->generic.log = log; return p_->generic.create_viewport(); }
uint32_t Device::set_viewport2(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) {
    p_->generic.log = log; return p_->generic.set_viewport2(id, x, y, w, h);
}
uint32_t Device::set_current_viewport(uint32_t id) { p_->generic.log = log; return p_->generic.set_current_viewport(id); }
uint32_t Device::viewport_clear(uint32_t id, uint32_t n, const Rect* rects, uint32_t flags) {
    p_->generic.log = log; return p_->generic.viewport_clear(id, n, rects, flags);
}
void Device::present() { p_->generic.log = log; p_->generic.present(); }
void Device::forget_texture(uint32_t h) { p_->generic.log = log; p_->generic.forget_texture(h); }
std::vector<TexFormat> Device::texture_formats() { return winx86::dx::d3d::Device::texture_formats(); }

// ---------------- Replayer ----------------
Vertex Replayer::to_clip(float x, float y, float u, float v, uint32_t w, uint32_t h) {
    return {2.f * (x + 0.5f) / float(w) - 1.f, 1.f - 2.f * (y + 0.5f) / float(h), 0.f, 1.f, u, v};
}
std::vector<uint32_t> Replayer::to_argb(TexFormat f, const uint8_t* px, uint32_t w, uint32_t h,
                                        uint32_t pitch, const uint32_t* pal, bool key,
                                        uint32_t lo, uint32_t hi) {
    std::vector<uint32_t> out(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = px + size_t(y) * pitch + x * bpp(f);
            uint32_t raw = 0, c = 0;
            switch (f) {
            case TexFormat::ARGB1555: raw = uint32_t(p[0] | p[1] << 8);
                c = ((raw & 0x8000) ? 0xFF000000u : 0) | e5(raw >> 10 & 31) << 16 | e5(raw >> 5 & 31) << 8 | e5(raw & 31); break;
            case TexFormat::RGB565: raw = uint32_t(p[0] | p[1] << 8);
                c = 0xFF000000u | e5(raw >> 11) << 16 | e6(raw >> 5 & 63) << 8 | e5(raw & 31); break;
            case TexFormat::ARGB4444: raw = uint32_t(p[0] | p[1] << 8);
                c = (raw >> 12) * 17 << 24 | (raw >> 8 & 15) * 17 << 16 | (raw >> 4 & 15) * 17 << 8 | (raw & 15) * 17; break;
            case TexFormat::ARGB8888: std::memcpy(&raw, p, 4); c = raw; break;
            case TexFormat::PAL8: raw = p[0]; c = pal ? pal[raw] : 0xFF000000u; break;
            }
            if (key && raw >= lo && raw <= hi) c &= 0x00FFFFFFu;
            out[size_t(y) * w + x] = c;
        }
    return out;
}
namespace {
// SPEC-062: tope de vértices por lote. vitaGL copia los 4 client arrays (36 B por vértice) al circular pool, cuya
// porción por frame es 2 MiB / 3 ≈ 699 KiB; un envío que la desborda cae a un alloc por reserva que en consola puede
// devolver NULL y dibujar igual (hipótesis de las texturas negras de e6749fb). 4095 × 36 B ≈ 144 KiB por envío; múltiplo
// de 3 para que las listas llenen el tope exacto. También queda lejos del índice U16 por defecto (49152).
constexpr size_t kBatchMaxVerts = 4095;
bool same_state(const DrawCall& a, const DrawCall& b) {
    return a.tex == b.tex && a.blend == b.blend && a.src == b.src && a.dst == b.dst &&
           a.alpha_test == b.alpha_test && a.linear == b.linear && a.scissor.x == b.scissor.x &&
           a.scissor.y == b.scissor.y && a.scissor.w == b.scissor.w && a.scissor.h == b.scissor.h;
}
size_t list_verts(const DrawCall& d) {
    const size_t n = d.verts.size();
    if (d.prim == 4) return n - n % 3;
    return n >= 3 ? 3 * (n - 2) : 0;
}
// Strip/fan → lista con los vértices de cada triángulo en el mismo orden en que RefRaster los arma. No se
// invierte el winding de los impares del strip: el culling está apagado (backend vitaGL y RefRaster), y rotar
// los vértices cambia el orden de la suma de baricéntricas en float (medido: ~14 px distintos por escena).
void append_list(std::vector<Vertex>& out, const DrawCall& d) {
    const auto& v = d.verts;
    const size_t n = v.size();
    if (d.prim == 4) { out.insert(out.end(), v.begin(), v.begin() + (n - n % 3)); return; }
    for (size_t i = 0; i + 2 < n; ++i) {
        if (d.prim == 6) out.insert(out.end(), {v[0], v[i + 1], v[i + 2]});
        else out.insert(out.end(), {v[i], v[i + 1], v[i + 2]});
    }
}
} // namespace

void Replayer::submit(DrawCall&& d) {
    ++draws_guest_;
    if (!batching_) { b_.draw(d); ++draws_emitted_; return; }
    if (batch_open_ && same_state(batch_, d) && list_verts(batch_) + list_verts(d) <= kBatchMaxVerts) {
        if (batch_.prim != 4) {
            std::vector<Vertex> list;
            append_list(list, batch_);
            batch_.verts.swap(list);
            batch_.prim = 4;
        }
        append_list(batch_.verts, d);
        return;
    }
    flush();
    batch_ = std::move(d);
    batch_open_ = true;
}
void Replayer::flush() {
    if (!batch_open_) return;
    b_.draw(batch_);
    ++draws_emitted_;
    batch_open_ = false;
}

void Replayer::play(const Ring& ring) {
    ring.for_each([&](uint16_t op, const uint32_t* p, uint32_t n) {
        switch (Op(op)) {
        case Op::Begin: if (n < 2) break;
            tw_ = p[0]; th_ = p[1];
            if (vp_.w == 0) vp_ = {0, 0, int32_t(tw_), int32_t(th_)};
            flush();
            b_.begin_frame(tw_, th_);
            tex_life_.begin_frame();
            break;
        case Op::Viewport: if (n >= 4) vp_ = {int32_t(p[0]), int32_t(p[1]), int32_t(p[2]), int32_t(p[3])}; break;
        case Op::Clear: flush(); if (n >= 5) b_.clear({int32_t(p[0]), int32_t(p[1]), int32_t(p[2]), int32_t(p[3])}, p[4]); break;
        case Op::State: if (n >= 2) rs_[p[0]] = p[1]; break;
        case Op::Tss: break;
        case Op::Texture: if (n >= 2) tex_ = p[1]; break;
        case Op::Draw: {
            if (n < 2 || n < 2 + p[1] * 6 || !tw_) break;
            if (tex_) tex_life_.use_texture(tex_);
            DrawCall d;
            d.prim = p[0];
            for (uint32_t i = 0; i < p[1]; ++i) {
                const uint32_t* q = p + 2 + i * 6;
                d.verts.push_back(to_clip(bitsf(q[0]), bitsf(q[1]), bitsf(q[4]), bitsf(q[5]), tw_, th_));
            }
            auto rs = [&](uint32_t s, uint32_t def) { auto it = rs_.find(s); return it == rs_.end() ? def : it->second; };
            d.tex = tex_;
            d.blend = rs(RS_ALPHABLENDENABLE, 0) != 0;
            d.src = rs(RS_SRCBLEND, 2); d.dst = rs(RS_DESTBLEND, 1);
            d.alpha_test = rs(RS_COLORKEYENABLE, 0) != 0 && keyed_[tex_];
            d.linear = rs(RS_TEXTUREMAG, 1) == 2;
            d.scissor = vp_;
            submit(std::move(d));
            break;
        }
        case Op::End: flush(); b_.end_frame(); break;
        case Op::Present:
            flush();
            tex_life_.present();
            // El free diferido sale **antes** del swap: la textura se libera antes de que el
            // frame que la usaba llegue a la pantalla.
            for (uint32_t id : tex_life_.drain()) b_.free_texture(id);
            b_.present();
            break;
        case Op::TexUpload: {
            flush();   // los draws del lote usan la textura anterior al upload
            if (n < 8) break;
            TextureData t;
            t.id = p[0]; t.width = p[1]; t.height = p[2]; t.format = TexFormat(p[3]);
            uint32_t pitch = p[4]; bool key = p[5] & 1;
            uint32_t format = uint32_t(t.format);
            if (!t.width || !t.height || format < uint32_t(TexFormat::ARGB1555) ||
                format > uint32_t(TexFormat::PAL8)) break;
            uint32_t pixel_bytes = bpp(t.format);
            uint64_t min_pitch = uint64_t(t.width) * pixel_bytes;
            if (min_pitch > pitch) break;
            t.keyed = key; t.key_low = p[6]; t.key_high = p[7];
            const uint32_t* pal = nullptr; uint32_t off = 8;
            if (t.format == TexFormat::PAL8) {
                pal = p + 8; off += 256;
            }
            uint64_t bytes64 = uint64_t(pitch) * t.height;
            uint64_t pixels64 = uint64_t(t.width) * t.height;
            uint64_t words64 = (bytes64 + 3) / 4;
            size_t size_max = std::numeric_limits<size_t>::max();
            if (bytes64 > size_max || pixels64 > size_max / sizeof(uint32_t) ||
                words64 > size_max - off || uint64_t(n) < uint64_t(off) + words64) break;
            size_t bytes = size_t(bytes64);
            keyed_[t.id] = key;
            const uint64_t content = texture_cache_
                ? texture_hash(t.width, t.height, pitch, t.format, key, p[6], p[7], pal,
                               pal ? 256 : 0, reinterpret_cast<const uint8_t*>(p + off), bytes)
                : 0;
            auto old = texture_hashes_.find(t.id);
            if (texture_cache_ && old != texture_hashes_.end() && old->second == content) {
                ++texture_cache_hits_;
                break; // identical source: avoid ARGB conversion, allocations and backend upload.
            }
            if (pal) t.palette.assign(pal, pal + 256);
            t.native.assign(reinterpret_cast<const uint8_t*>(p + off), reinterpret_cast<const uint8_t*>(p + off) + bytes);
            t.argb = to_argb(t.format, t.native.data(), t.width, t.height, pitch, pal, key, p[6], p[7]);
            // El nombre vuelve a designar otro objeto: el free diferido del mismo id queda obsoleto
            // (si no, borraría la textura viva que recién subimos).
            tex_life_.upload_texture(t.id);
            b_.upload_texture(t);
            if (texture_cache_) texture_hashes_[t.id] = content;
            ++texture_uploads_;
            break;
        }
        case Op::TexFree: flush(); if (n >= 1) {
            keyed_.erase(p[0]);
            texture_hashes_.erase(p[0]);
            // deliver=false con TEXDEFER=0: el delete real es inmediato (como el API) y el modelo
            // sólo registra el hazard (delete de un id que un frame no presentado puede estar leyendo).
            tex_life_.free_texture(p[0], texdefer_ > 0);
            if (!texdefer_) b_.free_texture(p[0]);
        } break;
        default: break;
        }
    });
    flush();   // el lote no cruza el fin del ring
    diag::DiagWindow* w = diag_window();
    if (w) {
        if (ringstat_ && (++diag_scenes_ & 3u) == 0) {
            const RingStats s = ring_stats(ring);
            w->add_ring(s.bad_verts(), s.verts, s.max_payload, s.max_edge);
        }
        w->tex_del_inflight = uint32_t(tex_life_.deletes_inflight());
        w->tex_up_inflight = uint32_t(tex_life_.reuploads_inflight());
    }
}

Replayer::Replayer(Backend& b, bool texture_cache) : b_(b), texture_cache_(texture_cache) {
    // Los toggles se leen al construir (env.txt ya los cargó al arranque): no se releen por frame.
    const char* batch = std::getenv("HW_D3D_BATCH");
    batching_ = !(batch && batch[0] == '0');
    const diag::Toggles t = diag::parse_toggles_env();
    if (!t.texcache) texture_cache_ = false;
    ringstat_ = t.ringstat;
    texdefer_ = t.texdefer;
    tex_life_.set_defer(texdefer_);
}

diag::DiagWindow* Replayer::diag_window() {
    // Perezoso: si el backend crea su ventana después de construirse el Replayer, no queda
    // nullptr para siempre (P2#4 de la review sonnet-rev).
    if (!diag_) diag_ = b_.diag_window();
    return diag_;
}

} // namespace hw::d3d
