// tests/test_hw_diag.cpp — HW-068 (SPEC-052 CA1..CA4): telemetría de diagnóstico BUG-047.
// CA1 parser de toggles HW_DIAG_* · CA2 lifetime de texturas (tex_del_inflight/TEXDEFER)
// CA3 circ_peak/overrun del pool circular · CA4 línea [hw] diag estable (parseable por tools/track_oracle.py).
#include "diag/hw_diag.h"
#include "d3d/d3d.h"
#include "d3d/ring_stats.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>

using namespace hw;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static diag::Getenv env_of(std::map<std::string, std::string> m) {
    return [m](const char* k) -> const char* {
        auto it = m.find(k);
        return it == m.end() ? nullptr : it->second.c_str();
    };
}

static uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

struct DiagBackend final : hw::d3d::Backend {
    diag::DiagWindow w;
    diag::DiagWindow* diag_window() override { return &w; }          // telemetría siempre-on
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const hw::d3d::TextureData&) override {}
    void free_texture(uint32_t) override {}
    void clear(const hw::d3d::Rect&, uint32_t) override {}
    void draw(const hw::d3d::DrawCall&) override {}
    void present() override {}
};

struct FreeCounterBackend final : hw::d3d::Backend {
    std::vector<uint32_t> freed;
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const hw::d3d::TextureData&) override {}
    void free_texture(uint32_t id) override { freed.push_back(id); }
    void clear(const hw::d3d::Rect&, uint32_t) override {}
    void draw(const hw::d3d::DrawCall&) override {}
    void present() override {}
};

// Escena mínima: textura 1 usada por un draw y liberada, más Present.
static hw::d3d::Ring tex_free_scene(uint32_t id) {
    hw::d3d::Ring r;
    r.put(d3d::Op::Begin, {640, 480});
    r.put(d3d::Op::Texture, {0, id});
    const float v[3][4] = {{0, 0, 0, 1}, {10, 0, 0, 1}, {0, 10, 0, 1}};
    std::vector<uint32_t> p{4, 3};
    for (auto& vert : v) { for (int k = 0; k < 4; ++k) p.push_back(fbits(vert[k]));
                           p.push_back(fbits(0)); p.push_back(fbits(0)); }
    r.put(d3d::Op::Draw, p.data(), uint32_t(p.size()));
    r.put(d3d::Op::TexFree, {id});
    r.put(d3d::Op::Present, {});
    r.put(d3d::Op::End, {});
    return r;
}

// Escena con subida de textura (4x4 ARGB1555, contenido fijo) + draw; opcional TexFree.
static hw::d3d::Ring upload_scene(uint32_t id, bool free_it) {
    hw::d3d::Ring r;
    r.put(d3d::Op::Begin, {640, 480});
    std::vector<uint32_t> up{id, 4, 4, uint32_t(winx86::dx::d3d::TexFormat::ARGB1555), 8, 0, 0, 0};
    up.insert(up.end(), 8, 0x3C00u);          // 32 bytes de píxeles
    r.put(d3d::Op::TexUpload, up.data(), uint32_t(up.size()));
    r.put(d3d::Op::Texture, {0, id});
    const float v[3][4] = {{0, 0, 0, 1}, {10, 0, 0, 1}, {0, 10, 0, 1}};
    std::vector<uint32_t> p{4, 3};
    for (auto& vert : v) { for (int k = 0; k < 4; ++k) p.push_back(fbits(vert[k]));
                           p.push_back(fbits(0)); p.push_back(fbits(0)); }
    r.put(d3d::Op::Draw, p.data(), uint32_t(p.size()));
    if (free_it) r.put(d3d::Op::TexFree, {id});
    r.put(d3d::Op::Present, {});
    r.put(d3d::Op::End, {});
    return r;
}

// Escena con sólo el TexFree del id (el nombre se libera sin volver a subirlo).
static hw::d3d::Ring free_scene(uint32_t id) {
    hw::d3d::Ring r;
    r.put(d3d::Op::Begin, {640, 480});
    r.put(d3d::Op::TexFree, {id});
    r.put(d3d::Op::Present, {});
    r.put(d3d::Op::End, {});
    return r;
}

// Backend que registra el orden de las operaciones del backend (para el orden free/present).
struct OrderBackend final : hw::d3d::Backend {
    std::vector<std::string> ops;
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const hw::d3d::TextureData&) override { ops.push_back("u"); }
    void free_texture(uint32_t) override { ops.push_back("f"); }
    void clear(const hw::d3d::Rect&, uint32_t) override {}
    void draw(const hw::d3d::DrawCall&) override {}
    void present() override { ops.push_back("p"); }
};

// Backend que detecta un free ejecutado después de la última subida de ese id (borra la textura viva).
struct HazardBackend final : hw::d3d::Backend {
    std::vector<std::string> ops;
    std::map<uint32_t, size_t> last_upload, last_free;
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const hw::d3d::TextureData& t) override {
        ops.push_back("u" + std::to_string(t.id));
        last_upload[t.id] = ops.size() - 1;
    }
    void free_texture(uint32_t id) override {
        ops.push_back("f" + std::to_string(id));
        last_free[id] = ops.size() - 1;
    }
    void clear(const hw::d3d::Rect&, uint32_t) override {}
    void draw(const hw::d3d::DrawCall&) override {}
    void present() override { ops.push_back("p"); }
    bool stale_free() const {
        for (const auto& e : last_free) {
            auto u = last_upload.find(e.first);
            if (u != last_upload.end() && e.second > u->second) return true;
        }
        return false;
    }
};

// Backend que cuenta las subidas reales (cache por contenido).
struct UploadCounterBackend final : hw::d3d::Backend {
    unsigned uploads = 0;
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const hw::d3d::TextureData&) override { ++uploads; }
    void free_texture(uint32_t) override {}
    void clear(const hw::d3d::Rect&, uint32_t) override {}
    void draw(const hw::d3d::DrawCall&) override {}
    void present() override {}
};

int main() {
    // ---- CA1: parser de toggles -------------------------------------------------
    {   // sin env.txt: defaults del VPK de usuario
        std::vector<std::string> w;
        diag::Toggles t = diag::parse_toggles(env_of({}), &w);
        CHECK(!t.ringstat && !t.drawlog && !t.glfinish && !t.gxmfinish && !t.poison && !t.canary);
        CHECK(t.texcache && t.texdefer == 0 && t.circ_mb == 2 && !t.sync_baseline);
        CHECK(w.empty());
    }
    {   // valores válidos
        std::vector<std::string> w;
        diag::Toggles t = diag::parse_toggles(env_of({
            {"HW_DIAG_RINGSTAT", "1"}, {"HW_DIAG_DRAWLOG", "1"}, {"HW_DIAG_TEXCACHE", "0"},
            {"HW_DIAG_TEXDEFER", "5"}, {"HW_DIAG_CIRC_MB", "16"}, {"HW_DIAG_GLFINISH", "1"},
            {"HW_D3D_SYNC_BASELINE", "1"}, {"HW_DIAG_POISON", "1"}, {"HW_DIAG_CANARY", "1"}}), &w);
        CHECK(t.ringstat && t.drawlog && !t.texcache && t.texdefer == 5 && t.circ_mb == 16);
        CHECK(t.glfinish && t.sync_baseline && t.poison && t.canary);
        CHECK(w.empty());
    }
    {   // inválidos: default + un warning por clave
        std::vector<std::string> w;
        diag::Toggles t = diag::parse_toggles(env_of({
            {"HW_DIAG_TEXDEFER", "abc"}, {"HW_DIAG_CIRC_MB", "1"}, {"HW_DIAG_TEXCACHE", "2"}}), &w);
        CHECK(t.texdefer == 0);     // no numérico → default
        CHECK(t.circ_mb == 2);      // <2 → fuera de rango → default
        CHECK(t.texcache);          // booleano inválido → default
        CHECK(w.size() == 3);
    }
    {   // CIRC_MB=99 → política única (P2#4): inválido → default 2 + warning (no recorte a 32)
        std::vector<std::string> w;
        diag::Toggles t = diag::parse_toggles(env_of({{"HW_DIAG_CIRC_MB", "99"}}), &w);
        CHECK(t.circ_mb == 2 && w.size() == 1);
    }
    {   // TEXDEFER=-3 → default 0 con warning
        std::vector<std::string> w;
        diag::Toggles t = diag::parse_toggles(env_of({{"HW_DIAG_TEXDEFER", "-3"}}), &w);
        CHECK(t.texdefer == 0 && w.size() == 1);
    }
    {   // TEXDEFER=9999 → fuera de rango → default 0 + warning (P2#4)
        std::vector<std::string> w;
        diag::Toggles t = diag::parse_toggles(env_of({{"HW_DIAG_TEXDEFER", "9999"}}), &w);
        CHECK(t.texdefer == 0 && w.size() == 1);
    }

    // ---- CA2: lifetime de texturas ---------------------------------------------
    {   // delete de un id usado en el frame abierto (no presentado) cuenta inflight; TEXDEFER=0 libera al presentar
        diag::TexLifetime t(0);
        t.begin_frame(); t.use_texture(5); t.free_texture(5);
        CHECK(t.deletes_inflight() == 1);
        t.present();
        std::vector<uint32_t> d = t.drain();
        CHECK(d.size() == 1 && d[0] == 5);
        CHECK(t.pending().empty());
    }
    {   // 3 frames en vuelo (productor adelantado al consumidor): uso no presentado = inflight
        diag::TexLifetime t(0);
        t.begin_frame(); t.use_texture(1);
        t.begin_frame(); t.use_texture(2);
        t.begin_frame(); t.use_texture(3);   // 3 frames abiertos, ninguno presentado
        t.free_texture(2);
        CHECK(t.deletes_inflight() == 1);
        CHECK(t.pending().size() == 1);
    }
    {   // TEXDEFER=N: el delete real ocurre N presents después
        diag::TexLifetime t(2);
        t.begin_frame(); t.use_texture(7); t.free_texture(7);
        CHECK(t.deletes_inflight() == 1);
        t.present();                              // f0
        t.begin_frame(); t.present();             // f1
        CHECK(t.drain().empty());                 // aún no venció
        t.begin_frame(); t.present();             // f2
        std::vector<uint32_t> d = t.drain();
        CHECK(d.size() == 1 && d[0] == 7);
    }
    {   // id ya presentado: delete inmediato, no inflight
        diag::TexLifetime t(0);
        t.begin_frame(); t.use_texture(9); t.present();
        t.free_texture(9);
        CHECK(t.deletes_inflight() == 0);
        std::vector<uint32_t> d = t.drain();
        CHECK(d.size() == 1 && d[0] == 9);
    }
    {   // cada id se libera una sola vez
        diag::TexLifetime t(1);
        t.begin_frame(); t.use_texture(4); t.free_texture(4);
        t.present(); t.begin_frame(); t.present();
        CHECK(t.drain().size() == 1);
        CHECK(t.drain().empty());
    }
    {   // P2#3: el diferimiento cuenta presents, no begins
        diag::TexLifetime t(2);
        t.begin_frame(); t.use_texture(7); t.free_texture(7);
        CHECK(t.deletes_inflight() == 1);
        t.begin_frame(); t.begin_frame(); t.begin_frame();   // begins extra, ningún present
        t.present();                                          // 1 present: faltan 2
        CHECK(t.drain().empty());
        t.present();                                          // 2 presents
        CHECK(t.drain().empty());
        t.present();                                          // 3 presents: vence (1 + defer 2)
        std::vector<uint32_t> d = t.drain();
        CHECK(d.size() == 1 && d[0] == 7);
    }
    {   // P2#2: doble free = una sola entrega y un solo conteo
        diag::TexLifetime t(2);
        t.begin_frame(); t.use_texture(7); t.free_texture(7);
        CHECK(t.deletes_inflight() == 1);
        t.free_texture(7);                                    // segundo free del mismo id: no-op
        CHECK(t.deletes_inflight() == 1);
        CHECK(t.pending().size() == 1);
        t.present(); t.begin_frame(); t.present(); t.begin_frame(); t.present();
        std::vector<uint32_t> d = t.drain();
        CHECK(d.size() == 1 && d[0] == 7);
        CHECK(t.drain().empty());
    }
    {   // P2#2: free de un id que nunca existió = no-op
        diag::TexLifetime t(0);
        t.free_texture(42);
        CHECK(t.deletes_inflight() == 0);
        CHECK(t.drain().empty());
    }
    {   // P2#2: reuso del nombre antes de que venza el delete pendiente lo deja obsoleto
        diag::TexLifetime t(2);
        t.begin_frame(); t.upload_texture(9);                 // el id queda vivo
        t.use_texture(9); t.free_texture(9);                  // delete pendiente (en vuelo)
        CHECK(t.pending().size() == 1);
        t.upload_texture(9);                                  // el nombre designa otro objeto
        CHECK(t.pending().empty());                           // el delete pendiente se canceló
        CHECK(t.reuploads_inflight() == 1);
        t.present(); t.begin_frame(); t.present(); t.begin_frame(); t.present();
        CHECK(t.drain().empty());                             // no borra el objeto nuevo
    }
    {   // re-upload de un id ya presentado: no cuenta inflight
        diag::TexLifetime t(0);
        t.begin_frame(); t.upload_texture(4);
        t.use_texture(4); t.present();
        t.upload_texture(4);
        CHECK(t.reuploads_inflight() == 0);
    }

    // ---- CA2/P2: asociación de fence por slot (rev-gfx) ------------------------
    {   // el slot envuelve cada buffer_count y la fence se asocia al frame submitido
        diag::FenceSlots f(3);
        CHECK(f.slot_for(0) == 0 && f.slot_for(3) == 0 && f.slot_for(4) == 1 && f.slot_for(5) == 2);
        f.submit(0, 10); f.submit(1, 11); f.submit(2, 12);
        CHECK(f.last_fence(0) == 10 && f.last_fence(1) == 11 && f.last_fence(2) == 12);
        CHECK(f.may_reuse(1, 0) && f.may_reuse(2, 0));   // sin vuelta aún
        CHECK(!f.may_reuse(3, 9));                       // fence del frame 0 no completó
        CHECK(f.may_reuse(3, 10));                       // asociación NO corrida un frame
        CHECK(!f.may_reuse(4, 10));                      // fence del frame 1 (11) no completó
    }
    {   // P2#1: sin submit no hay evidencia ⇒ no se reusa aunque la fence completada sea altísima
        diag::FenceSlots f(3);
        f.submit(1, 11);                                // el frame 0 nunca se sometió
        CHECK(!f.submitted(0) && f.submitted(1));
        CHECK(!f.may_reuse(3, UINT64_MAX));             // frame 0 sin evidencia ⇒ bloquea
        diag::FenceSlots g(3);
        g.submit(0, 10);
        CHECK(g.may_reuse(3, 10));                       // con evidencia, la fence alcanza
    }
    {   // P2#1: la tabla de fences no crece por frame (queda acotada a buffers_+1)
        diag::FenceSlots f(3);
        for (uint64_t i = 0; i < 1000; ++i) f.submit(i, i + 1);
        CHECK(f.tracked() <= 4u);
    }

    // ---- CA3: circ_peak / overrun ---------------------------------------------
    {
        diag::CircWindow c(2u * 1024 * 1024, 4);   // porción = 524288 exacto
        CHECK(c.portion() == 524288u);
        c.frame_begin(); c.add_draw(131072, 4); c.frame_end();   // == porción: sin overrun
        CHECK(c.overruns() == 0 && c.peak() == 524288u);
        c.frame_begin(); c.add_draw(131073, 4); c.frame_end();   // un byte más: overrun
        CHECK(c.overruns() == 1 && c.peak() == 524292u);
        c.frame_begin(); c.add_draw(100, 4); c.add_draw(200, 4); c.frame_end();
        CHECK(c.overruns() == 1);                                // suma del frame no supera la porción
        c.reset_window();
        CHECK(c.peak() == 0 && c.overruns() == 0);
    }
    {
        diag::CircWindow c(2u * 1024 * 1024, 3);   // porción = 699050 (división entera)
        CHECK(c.portion() == 699050u);
        c.frame_begin(); c.add_draw(174762, 4); c.frame_end();   // 699048 < porción
        CHECK(c.overruns() == 0);
        c.frame_begin(); c.add_draw(174763, 4); c.frame_end();   // 699052 > porción
        CHECK(c.overruns() == 1);
    }

    // ---- CA4: línea [hw] diag estable -----------------------------------------
    {
        diag::DiagWindow w;
        w.add_ring(7, 1000, 32776, 141.4);
        w.vgl_ram_free_min = 1234; w.vgl_vram_free_min = 5678;
        w.circ_peak = 524288; w.circ_overrun = 2; w.circ_wraps = 3; w.circ_slot = 1; w.circ_off = 4096;
        w.vgl_alloc_fail = 4; w.gl_err = 5;
        w.tex_live = 42; w.tex_bytes = 1048576; w.tex_up = 88; w.tex_del_inflight = 6; w.tex_up_inflight = 12;
        w.q_dropped = 9; w.q_carried = 10; w.canary_bad = 11;
        w.pool_enter_ram = 24000; w.pool_enter_vram = 64000;
        std::string line = diag::format_diag_line(w, "office", 10000);
        CHECK(line.rfind("[hw] diag ", 0) == 0);
        CHECK(line.find('\n') == std::string::npos);
        // Todas las claves del contrato presentes y con valor no vacío.
        const char* keys[] = {"context=office", "window_ms=10000",
            "ring_bad=7", "ring_verts=1000", "ring_maxn=32776", "ring_edge=141.4",
            "vgl_ram_free_min=1234", "vgl_vram_free_min=5678",
            "circ_peak=524288", "circ_overrun=2", "circ_wraps=3", "circ_slot=1", "circ_off=4096",
            "vgl_alloc_fail=4", "gl_err=5",
            "tex_live=42", "tex_bytes=1048576", "tex_up=88", "tex_del_inflight=6", "tex_up_inflight=12",
            "q_dropped=9", "q_carried=10", "canary_bad=11",
            "pool_enter_ram=24000", "pool_enter_vram=64000"};
        for (const char* k : keys) CHECK(line.find(k) != std::string::npos);
    }
    {   // Z/RHW NaN no cuentan como geometría mala (el replay los ignora)
        hw::d3d::Ring r;
        r.put(d3d::Op::Begin, {640, 480});
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<uint32_t> p{4, 3};
        const float v[3][4] = {{0, 0, nan, 1}, {10, 0, 0, nan}, {0, 10, 0, 1}};
        for (auto& vert : v) { for (int k = 0; k < 4; ++k) p.push_back(fbits(vert[k]));
                               p.push_back(fbits(0)); p.push_back(fbits(0)); }
        r.put(d3d::Op::Draw, p.data(), uint32_t(p.size()));
        hw::d3d::RingStats s = hw::d3d::ring_stats(r);
        CHECK(s.bad_verts() == 0 && s.nonfinite_zw == 2);
        diag::DiagWindow w;
        w.add_ring(uint64_t(s.bad_verts()), s.verts, s.max_payload, s.max_edge);
        CHECK(w.ring_bad == 0 && w.ring_verts == 3);
    }
    {   // acumulación de varias escenas
        diag::DiagWindow w;
        w.add_ring(2, 10, 100, 5.0);
        w.add_ring(3, 20, 32776, 200.0);
        CHECK(w.ring_bad == 5 && w.ring_verts == 30);
        CHECK(w.ring_maxn == 32776 && w.ring_edge_x10 == 2000);
    }

    // ---- CA4/replayer: acumulación de ring_stats con muestreo 1/4 ---------------
    {
        hw::d3d::Ring r;
        r.put(d3d::Op::Begin, {640, 480});
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<uint32_t> p{4, 3};
        const float v[3][4] = {{nan, 0, 0, 1}, {10, 0, 0, 1}, {0, 10, 0, 1}};
        for (auto& vert : v) { for (int k = 0; k < 4; ++k) p.push_back(fbits(vert[k]));
                               p.push_back(fbits(0)); p.push_back(fbits(0)); }
        r.put(d3d::Op::Draw, p.data(), uint32_t(p.size()));
        r.put(d3d::Op::End, {});

        DiagBackend b;
        setenv("HW_DIAG_RINGSTAT", "1", 1);
        hw::d3d::Replayer rep(b);                         // se auto-engancha a b.diag_window()
        unsetenv("HW_DIAG_RINGSTAT");
        for (int i = 0; i < 3; ++i) rep.play(r);          // escenas 1-3: sin muestreo
        CHECK(b.w.ring_verts == 0 && b.w.ring_bad == 0);
        rep.play(r);                                      // escena 4: muestrea
        CHECK(b.w.ring_bad == 1 && b.w.ring_verts == 3);
        for (int i = 0; i < 4; ++i) rep.play(r);          // escenas 5-8
        CHECK(b.w.ring_verts == 6);

        DiagBackend off;
        hw::d3d::Replayer rep2(off);                      // sin HW_DIAG_RINGSTAT: ventana sí, ring_* no
        for (int i = 0; i < 8; ++i) rep2.play(r);
        CHECK(off.w.ring_verts == 0 && off.w.ring_bad == 0);
    }

    // ---- TEXDEFER en el Replayer: el free al backend se difiere N presents ---------
    {
        hw::d3d::Ring r = tex_free_scene(1);
        FreeCounterBackend b;
        hw::d3d::Replayer rep(b);              // sin defer: free inmediato
        rep.play(r);
        CHECK(b.freed.size() == 1 && b.freed[0] == 1);
    }
    {
        hw::d3d::Ring r = tex_free_scene(1);
        FreeCounterBackend b;
        hw::d3d::Replayer rep(b);
        rep.set_texdefer(2);
        rep.play(r);
        CHECK(b.freed.empty());                // escena 0: diferido
        rep.play(r);
        CHECK(b.freed.empty());                // escena 1: aún no venció
        rep.play(r);
        CHECK(b.freed.size() == 1 && b.freed[0] == 1);   // escena 2: vence
    }

    // ---- P1 sonnet-rev: el free diferido no puede borrar una textura re-subida ----
    {
        HazardBackend b;
        hw::d3d::Replayer rep(b);
        rep.set_texdefer(1);
        rep.play(upload_scene(5, true));       // frame0: subida + uso + free (diferido)
        rep.play(upload_scene(5, false));      // frame1: el guest re-sube el id 5
        rep.play(upload_scene(5, false));      // frame2
        rep.play(upload_scene(5, false));      // frame3
        CHECK(!b.stale_free());                // ningún free después de la re-subida
    }
    {   // control: sin defer el free sale antes de la re-subida
        HazardBackend b;
        hw::d3d::Replayer rep(b);
        rep.play(upload_scene(5, true));
        rep.play(upload_scene(5, false));
        CHECK(!b.stale_free());
        CHECK(b.ops.size() == 5 && b.ops[0] == "u5" && b.ops[1] == "f5");
    }

    // ---- orden del drenaje: el free sale antes del swap, nunca después ----
    {
        OrderBackend b;
        hw::d3d::Replayer rep(b);
        rep.set_texdefer(1);
        rep.play(tex_free_scene(1));
        rep.play(tex_free_scene(1));
        CHECK(b.ops.size() == 3 && b.ops[0] == "p" && b.ops[1] == "f" && b.ops[2] == "p");
    }

    // ---- el nombre liberado no reutiliza la entrada de caché: hay que volver a subir ----
    {
        UploadCounterBackend b;
        hw::d3d::Replayer rep(b);
        rep.play(upload_scene(5, false));      // subida id 5
        rep.play(free_scene(5));               // el nombre se libera
        rep.play(upload_scene(5, false));      // mismo contenido, pero el nombre estaba libre
        CHECK(b.uploads == 2);
    }

    // ---- P1 sonnet-rev: HW_DIAG_TEXDEFER cableado (env → comportamiento) ----
    {
        setenv("HW_DIAG_TEXDEFER", "1", 1);
        FreeCounterBackend b;
        hw::d3d::Replayer rep(b);              // el toggle se lee al construir el Replayer
        rep.play(tex_free_scene(1));
        CHECK(b.freed.empty());                // diferido por env
        unsetenv("HW_DIAG_TEXDEFER");
    }
    {   // sin el toggle, el free es inmediato (comportamiento del API)
        FreeCounterBackend b;
        hw::d3d::Replayer rep(b);
        rep.play(tex_free_scene(1));
        CHECK(b.freed.size() == 1 && b.freed[0] == 1);
    }

    std::printf(fails ? "test_hw_diag: %d FALLOS\n" : "test_hw_diag: OK\n", fails);
    return fails ? 1 : 0;
}
