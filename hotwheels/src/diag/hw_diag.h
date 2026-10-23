// hw_diag.h — HW-068 (SPEC-052): telemetría de diagnóstico de BUG-047 para la Fase 2 en consola.
// Modelo host puro (header-only, sin hilos ni estado global): lo invoca el hilo que ya posee el dato.
// Contrato y razonamiento SO/hardware en specs/052-vpk-diagnostico-{bug047,implementacion}.md.
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hw::diag {

using Getenv = std::function<const char*(const char*)>;

namespace detail {
inline bool parse_int(const char* s, long& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s, &end, 10);
    if (errno || end == s || *end) return false;
    out = v;
    return true;
}
} // namespace detail

// Booleano estricto: "1" on, "0" off; cualquier otra cosa deja el default y avisa.
inline bool env_flag(const Getenv& get, const char* key, bool def, std::vector<std::string>* warn = nullptr) {
    const char* s = get(key);
    if (!s) return def;
    if (!std::strcmp(s, "1")) return true;
    if (!std::strcmp(s, "0")) return false;
    if (warn) warn->push_back(std::string(key) + "='" + s + "' invalido; uso " + (def ? "1" : "0"));
    return def;
}

// Entero con rango: no numérico o fuera de [lo,hi] → default + warning (política única, CA1).
inline int env_int_range(const Getenv& get, const char* key, int def, int lo, int hi,
                         std::vector<std::string>* warn = nullptr) {
    const char* s = get(key);
    if (!s) return def;
    long v = 0;
    if (!detail::parse_int(s, v)) {
        if (warn) warn->push_back(std::string(key) + "='" + s + "' no numerico; uso " + std::to_string(def));
        return def;
    }
    if (v < lo || v > hi) {
        if (warn) warn->push_back(std::string(key) + "=" + s + " fuera de [" + std::to_string(lo) + "," +
                                  std::to_string(hi) + "]; uso " + std::to_string(def));
        return def;
    }
    return int(v);
}

// Toggles del contrato SPEC-052; se leen una vez al arranque (antes de crear el backend).
struct Toggles {
    bool ringstat = false;      // HW_DIAG_RINGSTAT=1
    bool drawlog = false;       // HW_DIAG_DRAWLOG=1
    bool glfinish = false;      // HW_DIAG_GLFINISH=1
    bool gxmfinish = false;     // HW_DIAG_GXMFINISH=1
    bool poison = false;        // HW_DIAG_POISON=1
    bool canary = false;        // HW_DIAG_CANARY=1 (alias HW_DIAG_TRACE_DRAW=1)
    bool texcache = true;       // HW_DIAG_TEXCACHE=0 desactiva la caché por contenido
    int texdefer = 0;           // HW_DIAG_TEXDEFER=N marcos antes del glDeleteTextures real
    int circ_mb = 2;            // HW_DIAG_CIRC_MB=2..32 MiB
    bool sync_baseline = false; // HW_D3D_SYNC_BASELINE=1: sin pipeline de 3 slots
};

inline Toggles parse_toggles(const Getenv& get, std::vector<std::string>* warn = nullptr) {
    Toggles t;
    t.ringstat = env_flag(get, "HW_DIAG_RINGSTAT", false, warn);
    t.drawlog = env_flag(get, "HW_DIAG_DRAWLOG", false, warn);
    t.glfinish = env_flag(get, "HW_DIAG_GLFINISH", false, warn);
    t.gxmfinish = env_flag(get, "HW_DIAG_GXMFINISH", false, warn);
    t.poison = env_flag(get, "HW_DIAG_POISON", false, warn);
    t.canary = env_flag(get, "HW_DIAG_CANARY", false, warn) | env_flag(get, "HW_DIAG_TRACE_DRAW", false, warn);
    t.texcache = env_flag(get, "HW_DIAG_TEXCACHE", true, warn);
    t.sync_baseline = env_flag(get, "HW_D3D_SYNC_BASELINE", false, warn);
    t.texdefer = env_int_range(get, "HW_DIAG_TEXDEFER", 0, 0, 1024, warn);
    t.circ_mb = env_int_range(get, "HW_DIAG_CIRC_MB", 2, 2, 32, warn);
    return t;
}

// Toggles del proceso (env.txt ya los cargó en el entorno al arranque). Se leen al construir cada
// consumidor (backend y Replayer): no se releen por frame, así no hay carrera con el reporte.
inline Toggles parse_toggles_env(std::vector<std::string>* warn = nullptr) {
    return parse_toggles([](const char* k) { return std::getenv(k); }, warn);
}

// Vida útil de un nombre de textura: decide si un TexFree puede ejecutarse ya o debe diferirse
// porque un frame aún no presentado (en vuelo) puede estar leyéndolo.
// No acredita GPU idle al presentar: swap sólo deja de ser "no presentado" en CPU.
//
// Contrato (spec 052 anexo, P2#2/P2#3):
//  · un id vivo se libera **una sola vez**: el doble free es no-op;
//  · el diferimiento se cuenta en **presents**, no en begins: vence en el present que libera el
//    frame que usó la textura + `defer` presents extra;
//  · el re-upload del mismo nombre deja obsoleto el delete pendiente (el objeto nuevo manda).
class TexLifetime {
public:
    explicit TexLifetime(int defer_presents = 0) : defer_(defer_presents < 0 ? 0 : defer_presents) {}

    void set_defer(int presents) { defer_ = presents < 0 ? 0 : presents; }
    void begin_frame() { ++frame_; }
    void present() {
        ++presents_;
        presented_ = frame_;
        for (size_t i = 0; i < sched_.size();) {
            const Sched& s = sched_[i];
            if (s.deadline <= presents_ && presented_ >= s.use_frame) {
                ready_.push_back(s.id);
                sched_.erase(sched_.begin() + long(i));
            } else {
                ++i;
            }
        }
    }
    void use_texture(uint32_t id) {
        // Camino rápido: la misma textura en el mismo frame (el caso normal de un draw loop).
        if (id == last_used_id_ && frame_ == last_used_frame_) return;
        last_used_id_ = id;
        last_used_frame_ = frame_;
        IdState& st = ids_[id];
        st.live = true;
        st.last_use = frame_;
    }
    // Re-creación del mismo nombre: el delete pendiente (si lo había) quedó obsoleto.
    void upload_texture(uint32_t id) {
        auto it = ids_.find(id);
        const int last_use = it != ids_.end() ? it->second.last_use : -1;
        if (last_use > presented_) ++reuploads_inflight_;
        cancel_pending(id);
        IdState& st = ids_[id];
        st.live = true;
        st.last_use = -1;
    }
    // `deliver` true: el modelo entrega el nombre (lo difiere si el id estaba en vuelo).
    // `deliver` false: el llamador hace el delete ahora y el modelo sólo registra el hazard.
    void free_texture(uint32_t id, bool deliver = true) {
        auto it = ids_.find(id);
        if (it == ids_.end() || !it->second.live) return;   // doble free o id desconocido: no-op
        const int last_use = it->second.last_use;
        it->second.live = false;
        const bool inflight = last_use > presented_;
        if (inflight) ++deletes_inflight_;
        if (!deliver) return;
        if (!inflight) {
            ready_.push_back(id);
            return;
        }
        // Vence en el present que libera el frame que la usó, más `defer` presents extra.
        sched_.push_back({id, presents_ + uint64_t(defer_) + 1, last_use});
    }
    // Nombres cuyo glDeleteTextures debe ejecutarse ahora (vaciado por presents o por el llamador).
    std::vector<uint32_t> drain() { return std::exchange(ready_, {}); }
    std::vector<uint32_t> pending() const {
        std::vector<uint32_t> out;
        out.reserve(sched_.size());
        for (const auto& e : sched_) out.push_back(e.id);
        return out;
    }
    uint64_t deletes_inflight() const { return deletes_inflight_; }
    uint64_t reuploads_inflight() const { return reuploads_inflight_; }

private:
    struct IdState { bool live = false; int last_use = -1; };
    struct Sched { uint32_t id; uint64_t deadline; int use_frame; };

    void cancel_pending(uint32_t id) {
        for (size_t i = 0; i < sched_.size();) {
            if (sched_[i].id == id) sched_.erase(sched_.begin() + long(i));
            else ++i;
        }
    }

    int defer_ = 0;
    int frame_ = -1, presented_ = -1;      // frame abierto / frame del último present
    uint64_t presents_ = 0;                // presents realizados (unidad del diferimiento)
    std::map<uint32_t, IdState> ids_;
    std::vector<Sched> sched_;
    std::vector<uint32_t> ready_;
    uint32_t last_used_id_ = UINT32_MAX;
    int last_used_frame_ = -2;
    uint64_t deletes_inflight_ = 0, reuploads_inflight_ = 0;
};

// Invariante de la cola de display: la porción del frame N se reusa en N+buffers y sólo cuando la
// fence asociada al frame que la usó completó. Detecta una asociación corrida un frame (fence de
// otro frame), que haría sobrescribir bytes que la GPU todavía lee.
// P2#1: un frame **no sometida** no tiene evidencia ⇒ bloquea el reuso (no es "fence 0 completada",
// porque 0 también es una fence legítima).
class FenceSlots {
public:
    explicit FenceSlots(unsigned buffers) : buffers_(buffers ? buffers : 1) {}

    unsigned buffers() const { return buffers_; }
    unsigned slot_for(uint64_t frame) const { return unsigned(frame % buffers_); }
    void submit(uint64_t frame, uint64_t fence) {
        fence_[frame] = fence;
        // La tabla no crece por frame: alcanza con la fence del frame que ocupa el slot.
        while (fence_.size() > size_t(buffers_) + 1) fence_.erase(fence_.begin());
    }
    bool submitted(uint64_t frame) const { return fence_.count(frame) != 0; }
    uint64_t last_fence(uint64_t frame) const {
        auto it = fence_.find(frame);
        return it == fence_.end() ? 0 : it->second;
    }
    size_t tracked() const { return fence_.size(); }
    bool may_reuse(uint64_t frame, uint64_t completed_fence) const {
        if (frame < buffers_) return true;                 // el slot nunca giró
        auto it = fence_.find(frame - buffers_);
        if (it == fence_.end()) return false;               // sin evidencia de submit ⇒ no se reusa
        return completed_fence >= it->second;
    }

private:
    unsigned buffers_;
    std::map<uint64_t, uint64_t> fence_;
};

// Presión de vértices del pool circular: suma bytes por frame y cuenta los que exceden pool/display_buffers.
class CircWindow {
public:
    CircWindow(uint32_t pool_bytes, uint32_t portions)
        : portion_(portions ? pool_bytes / portions : pool_bytes) {}

    uint32_t portion() const { return portion_; }
    void frame_begin() { frame_bytes_ = 0; }
    void add_draw(uint32_t verts, uint32_t stride_bytes) {
        frame_bytes_ += uint64_t(verts) * stride_bytes;
    }
    void frame_end() {
        peak_ = std::max(peak_, frame_bytes_);
        if (frame_bytes_ > portion_) ++overruns_;
    }
    void reset_window() { peak_ = 0; overruns_ = 0; }
    uint64_t peak() const { return peak_; }
    uint32_t overruns() const { return overruns_; }

private:
    uint32_t portion_;
    uint64_t frame_bytes_ = 0;
    uint64_t peak_ = 0;
    uint32_t overruns_ = 0;
};

// Contadores de la ventana de reporte `[hw] diag`. `add_ring` usa sólo x/y/u/v (bad_verts); z/rhw NaN quedan fuera.
//
// El escritor es el hilo que reproduce el ring (consumer); el lector es el hilo que reporta
// (mismo hilo en interp60, otro en fps_mode=original). Por eso los contadores son atómicos
// relajados: son estadística (nada depende del orden) y no pueden partirse a mitad de un read.
struct DiagWindow {
    std::atomic<uint64_t> ring_bad{0}, ring_verts{0};
    std::atomic<uint32_t> ring_maxn{0};
    std::atomic<uint32_t> ring_edge_x10{0};      // arista máx ×10 (se imprime con un decimal)
    std::atomic<uint32_t> vgl_ram_free_min{0}, vgl_vram_free_min{0};
    std::atomic<uint64_t> circ_peak{0};
    std::atomic<uint32_t> circ_overrun{0}, circ_wraps{0}, circ_slot{0}, circ_off{0};
    std::atomic<uint64_t> vgl_alloc_fail{0}, gl_err{0};
    std::atomic<uint32_t> tex_live{0}, tex_bytes{0}, tex_up{0}, tex_del_inflight{0}, tex_up_inflight{0};
    std::atomic<uint32_t> q_dropped{0}, q_carried{0};
    std::atomic<uint64_t> canary_bad{0};
    std::atomic<uint32_t> pool_enter_ram{0}, pool_enter_vram{0};

    void add_ring(uint64_t bad, uint64_t verts, uint32_t maxn, double edge) {
        ring_bad.fetch_add(bad, std::memory_order_relaxed);
        ring_verts.fetch_add(verts, std::memory_order_relaxed);
        uint32_t cur = ring_maxn.load(std::memory_order_relaxed);
        while (maxn > cur &&
               !ring_maxn.compare_exchange_weak(cur, maxn, std::memory_order_relaxed)) {}
        const uint32_t e10 = uint32_t(edge * 10.0 + 0.5);
        uint32_t cure = ring_edge_x10.load(std::memory_order_relaxed);
        while (e10 > cure &&
               !ring_edge_x10.compare_exchange_weak(cure, e10, std::memory_order_relaxed)) {}
    }
};

// La ventana es **acumulada desde el arranque**: el lector no la reinicia (evita la carrera con
// el escritor) y el usuario saca los deltas entre dos líneas consecutivas.
inline std::string format_diag_line(const DiagWindow& w, const char* context, uint32_t window_ms) {
    char buf[512];
    const double edge = w.ring_edge_x10.load(std::memory_order_relaxed) / 10.0;
    std::snprintf(buf, sizeof buf,
        "[hw] diag context=%s window_ms=%u ring_bad=%llu ring_verts=%llu ring_maxn=%u ring_edge=%.1f "
        "vgl_ram_free_min=%u vgl_vram_free_min=%u circ_peak=%llu circ_overrun=%u circ_wraps=%u circ_slot=%u "
        "circ_off=%u vgl_alloc_fail=%llu gl_err=%llu tex_live=%u tex_bytes=%u tex_up=%u tex_del_inflight=%u "
        "tex_up_inflight=%u q_dropped=%u q_carried=%u canary_bad=%llu pool_enter_ram=%u pool_enter_vram=%u",
        context, window_ms,
        (unsigned long long)w.ring_bad.load(std::memory_order_relaxed),
        (unsigned long long)w.ring_verts.load(std::memory_order_relaxed),
        w.ring_maxn.load(std::memory_order_relaxed), edge,
        w.vgl_ram_free_min.load(std::memory_order_relaxed),
        w.vgl_vram_free_min.load(std::memory_order_relaxed),
        (unsigned long long)w.circ_peak.load(std::memory_order_relaxed),
        w.circ_overrun.load(std::memory_order_relaxed), w.circ_wraps.load(std::memory_order_relaxed),
        w.circ_slot.load(std::memory_order_relaxed), w.circ_off.load(std::memory_order_relaxed),
        (unsigned long long)w.vgl_alloc_fail.load(std::memory_order_relaxed),
        (unsigned long long)w.gl_err.load(std::memory_order_relaxed),
        w.tex_live.load(std::memory_order_relaxed), w.tex_bytes.load(std::memory_order_relaxed),
        w.tex_up.load(std::memory_order_relaxed), w.tex_del_inflight.load(std::memory_order_relaxed),
        w.tex_up_inflight.load(std::memory_order_relaxed),
        w.q_dropped.load(std::memory_order_relaxed), w.q_carried.load(std::memory_order_relaxed),
        (unsigned long long)w.canary_bad.load(std::memory_order_relaxed),
        w.pool_enter_ram.load(std::memory_order_relaxed), w.pool_enter_vram.load(std::memory_order_relaxed));
    return std::string(buf);
}

} // namespace hw::diag
