// src/integration/dsound_bridge.cpp — ds_emul (winx86) → Engine HW-018.
//
// winx86 ds_emul elige su sink llamando a d2rt::audio::make_wav_sink /
// make_vita_sink / make_null_sink y thread_*. Este TU DEFINE esos símbolos
// (los .o del port van antes de libwinx86 en el link, así que ganan y el
// miembro de audio de winx86 no se arrastra). Nuestro sink entrega el PCM al
// Engine de HW-018, que mezcla la música de HW-015 y saca por WAV (host) o
// sceAudioOut (Vita).
#include "integration/dsound_bridge.h"
#include "diag/thread_cpu.h"

#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <pthread.h>
#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#endif
#include <cstring>

#include "audio/ds_bridge.h"
#include "audio/engine.h"
#include "audio/vita_sink.h"  // kAudioCpuMask/kAudioThreadPriority: scheduling del hilo de audio
#include "integration/audio_metrics_gate.h"
#include "integration/dsound_trace.h"
#include "runtime/audio_sink.h"  // d2rt::audio::Sink / make_* / thread_* / wav_check
#include "runtime/ds_emul.h"

namespace hw::integration {
namespace {

// Log DURABLE de audio (pedido integ, crash Vita3K tras DirectSoundCreate):
// cada línea abre/escribe/cierra, así sobrevive a un crash inmediato posterior.
// Vita: ux0:data/hotwheels/audio.log. Host: stderr.
void alog(const char* fmt, ...) {
    char b[256];
    va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
#if defined(__vita__)
    if (FILE* f = std::fopen("ux0:data/hotwheels/audio.log", "a")) { std::fputs(b, f); std::fputc('\n', f); std::fclose(f); }
#else
    std::fprintf(stderr, "[audio] %s\n", b);
#endif
}

DsoundConfig g_cfg;
DsoundTrace g_trace;
std::atomic<bool> g_stop{false};   // pide parar al hilo self-paced (write() deja de esperar)
bool self_paced_on() { return g_cfg.vita && g_cfg.self_paced; }
std::string g_write_root;  // persiste: HostOps.write_root apunta acá

std::string resolve_music(const std::string& win) {
    std::string p = win;
    for (auto& c : p) if (c == '\\') c = '/';
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    const std::string direct = g_cfg.game_root.empty() ? p : g_cfg.game_root + "/" + p;
    if (FILE* f = std::fopen(direct.c_str(), "rb")) { std::fclose(f); alog("music: %s -> %s", win.c_str(), direct.c_str()); return direct; }
    // El repack deja las pistas fuera de Program_Files: extracted/music/TrackN.wav
    // (decisiones.md 2026-10-03). Probar ese layout antes de rendirse (silencio).
    const std::string base = p.substr(p.find_last_of('/') + 1);
    const std::string repack = g_cfg.game_root + "/../../music/" + base;
    if (FILE* f = std::fopen(repack.c_str(), "rb")) { std::fclose(f); alog("music: %s -> %s", win.c_str(), repack.c_str()); return repack; }
    alog("music: %s -> %s (NO EXISTE: silencio)", win.c_str(), direct.c_str());
    return direct;
}

void set_env(const char* k, const char* v) {
#if defined(_WIN32)
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

// Sink que ds_emul cree estar abriendo: en realidad alimenta el Engine.
class EngineSink : public d2rt::audio::Sink {
public:
    bool open(int freq, int ch, int grain) override {
        alog("sink.open freq=%d ch=%d grain=%d vita=%d out=%d", freq, ch, grain, (int)g_cfg.vita, g_cfg.out_rate);
        hw::audio::EngineConfig ec;
        ec.out_rate = g_cfg.out_rate > 0 ? g_cfg.out_rate : hw::audio::kSampleRate;
        ec.wav_path = g_cfg.wav_path;
        ec.vita = g_cfg.vita;
        ec.grain = grain > 0 ? grain : g_cfg.grain;
        ec.music_resolver = &resolve_music;
        alog("engine: start ...");
        if (!hw::audio::ds_backend_start(ec)) { alog("engine: start FALLÓ"); return false; }
        alog("engine: start ok");
        src_rate_ = freq > 0 ? freq : g_cfg.mix_rate;
        return true;
    }
    void write(const int16_t* pcm, int frames) override {
        if (!fed_) { fed_ = true; alog("sink.write primera: frames=%d pcm=%p", frames, (const void*)pcm); }
        hw::audio::ds_backend_feed(pcm, frames, src_rate_);
        fed_frames_ += (uint64_t)frames;
        if (self_paced_on()) {
            // Contrapresión: el hilo de ds_emul produce al ritmo del consumidor (sceAudioOut),
            // no del guest; espera acotada y reintenta para poder parar (g_stop).
            if (auto* engine = hw::audio::ds_backend_engine()) {
                auto& bus = engine->dsound();
                while (!g_stop.load(std::memory_order_relaxed) && !bus.wait_below(bus.prime_ms() + 60, 20)) {}
            }
        }
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        if (metrics_gate_.due(now_ms)) {
            if (auto* engine = hw::audio::ds_backend_engine()) {
                alog("metrics monotonic_ms=%llu pcm_frames=%llu dSound_underruns=%llu dSound_overruns=%llu",
                     (unsigned long long)now_ms, (unsigned long long)fed_frames_,
                     (unsigned long long)engine->dsound().underruns(),
                     (unsigned long long)engine->dsound().overruns());
            }
        }
    }
    void close() override { g_trace.close(); }
    const char* name() const override { return g_cfg.vita ? "hw-vita" : "hw-wav"; }
    int rest_samples() override { return -1; }
    // false: el frame-tick del guest bombea (host WAV, determinista). true (Vita, flag): hilo propio
    // de ds_emul a ritmo de pared, independiente de los fps del guest (BUG-031).
    bool self_paced() const override { return self_paced_on(); }

private:
    int src_rate_ = 22050;
    bool fed_ = false;
    uint64_t fed_frames_ = 0;
    hw::integration::AudioMetricsGate metrics_gate_;
};

class NullSink : public d2rt::audio::Sink {
public:
    bool open(int, int, int) override { return true; }
    void write(const int16_t*, int) override {}
    void close() override {}
    const char* name() const override { return "hw-null"; }
};

}  // namespace

bool hw_register_dsound(d2rt::Bridge& br, d2rt::Cpu& cpu, const DsoundConfig& cfg, std::string& err) {
    alog("register: vita=%d mix=%d out=%d grain=%d wav=%s game=%s", (int)cfg.vita, cfg.mix_rate,
         cfg.out_rate, cfg.grain, cfg.wav_path.c_str(), cfg.game_root.c_str());
    if (!cfg.va_alloc || !cfg.tick_ms) { err = "dsound: faltan va_alloc/tick_ms"; return false; }
    if (cfg.mix_rate <= 0) { err = "dsound: mix_rate inválido"; return false; }

    g_cfg = cfg;
    // Default ON en Vita (gate cumplido en consola, opus-selfpaced-audio.log: 101,7 % de tiempo
    // real, 0 underruns, hilo rc=0). HW_AUDIO_SELF_PACED=0 vuelve al frame-tick del guest.
    g_cfg.self_paced = cfg.vita;
    if (const char* e = std::getenv("HW_AUDIO_SELF_PACED")) g_cfg.self_paced = (*e == '1');
    alog("audio self_paced=%d (vita=%d)", (int)(g_cfg.vita && g_cfg.self_paced), (int)g_cfg.vita);
    g_write_root = cfg.write_root;
    // ds_emul lee el sink al hacer DirectSoundCreate; forzamos el nuestro.
    set_env("WX86_SON", cfg.vita ? "vita" : "wav");
    if (!cfg.vita && !cfg.wav_path.empty()) set_env("WX86_SONDUMP", cfg.wav_path.c_str());

    d2rt::dsound::HostOps ops;
    ops.va_alloc = cfg.va_alloc;
    ops.tick_ms = cfg.tick_ms;
    ops.write_root = g_write_root.empty() ? nullptr : g_write_root.c_str();
    ops.mix_rate = cfg.mix_rate;
    if (const char* path = std::getenv("HW_DS_TRACE")) {
        if (*path && std::strcmp(path, "0") != 0) {
#ifdef WX86_DS_TRACE_API
            uint64_t cap = 64ull * 1024 * 1024;
            if (const char* limit = std::getenv("HW_DS_TRACE_MAX_BYTES")) {
                char* end = nullptr;
                cap = std::strtoull(limit, &end, 10);
                if (!end || *end || cap < 24 || cap > 64ull * 1024 * 1024) {
                    err = "dsound: cota de trace inválida (24..67108864)";
                    return false;
                }
            }
            if (!g_trace.open(path, cap)) { err = "dsound: no se pudo abrir HW_DS_TRACE"; return false; }
            ops.trace = [](uint32_t kind, const uint32_t* words, uint32_t n, const void* data, uint32_t bytes) {
                g_trace.event(kind, words, n, data, bytes);
            };
            alog("trace diagnóstica=%s cap=%llu (intrusiva)", path, (unsigned long long)cap);
#else
            err = "dsound: HW_DS_TRACE requiere parche winx86-0013";
            return false;
#endif
        }
    }
    alog("ds_emul: install ...");
    d2rt::dsound::install(br, cpu, ops);
    if (!d2rt::dsound::enabled()) { alog("ds_emul: NO armado"); err = "dsound: ds_emul no se armó"; return false; }
    alog("ds_emul: armado (DirectSoundCreate registrado)");
    return true;
}

hw::winmm::MusicSink* hw_dsound_music() { return hw::audio::ds_backend_music(); }

}  // namespace hw::integration

// ---- overrides de los símbolos de audio de winx86 --------------------------
namespace d2rt { namespace audio {

Sink* make_wav_sink(const char*) { hw::integration::alog("make_wav_sink"); return new hw::integration::EngineSink(); }
Sink* make_null_sink() { hw::integration::alog("make_null_sink"); return new hw::integration::NullSink(); }
Sink* make_vita_sink() {
    hw::integration::alog("make_vita_sink vita=%d", (int)hw::integration::g_cfg.vita);
    return hw::integration::g_cfg.vita ? (Sink*)new hw::integration::EngineSink() : nullptr;
}
// Hilo de ds_emul (self-paced): pthread (pte en Vita; el body toma std::mutex y usa newlib, ver
// vita_sink.cpp). Sólo se llega acá si EngineSink::self_paced() es true.
namespace {
pthread_t g_th; bool g_th_up = false; void (*g_body)(void) = nullptr;
unsigned g_mask = 0;   // resuelto en el hilo llamador como en el sink (getenv/TLS)
void* th_entry(void*) {
    hw::diag::ThreadRegistration registration(hw::diag::ThreadRole::Dsound);
#if defined(__vita__)
    const int thid = (int)sceKernelGetThreadId();
    // La prioridad del kernel es ABSOLUTA (64..191); 0x1000xxxx es RELATIVA al
    // default del proceso (0x100000E0 = DEFAULT-32). Se lee la de ANTES para
    // poder comparar el valor EFECTIVO.
    const int prio_before = sceKernelGetThreadCurrentPriority();
    const int rcp = sceKernelChangeThreadCpuAffinityMask(thid, (int)g_mask);
    const int rcr = sceKernelChangeThreadPriority(thid, hw::audio::kAudioThreadPriority);
    // Readback: si el kernel no aplicó el mask o la prioridad, el hilo NO está
    // donde se cree y la corrida no sirve como medición de scheduling.
    const int got_mask = sceKernelGetThreadCpuAffinityMask(thid);
    const int got_prio = sceKernelGetThreadCurrentPriority();
    const bool mask_ok = (rcp == 0) && (got_mask == (int)g_mask);
    const bool prio_ok = (rcr == 0) && (got_prio == hw::audio::kAudioThreadPriorityAbs);
    hw::integration::alog("ds thread rc_pin=%d rc_prio=%d mask=0x%x leido=0x%x prio_antes=0x%x "
                          "prio_efectiva=0x%x prio_esperada=0x%x afinidad=%s prioridad=%s",
                          rcp, rcr, g_mask, (unsigned)got_mask, (unsigned)prio_before,
                          (unsigned)got_prio, (unsigned)hw::audio::kAudioThreadPriorityAbs,
                          mask_ok ? "OK" : "INVALIDA (medición de scheduling no vale)",
                          prio_ok ? "OK" : "INVALIDA");
#endif
    g_body();
    return nullptr;
}
bool th_spawn() {
    if (g_th_up) return true;
    g_mask = hw::audio::audio_thread_cpu_mask();
    hw::integration::g_stop.store(false);
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64 * 1024);
    const int rc = pthread_create(&g_th, &at, &th_entry, nullptr);
    pthread_attr_destroy(&at);
    hw::integration::alog("ds thread_start rc=%d", rc);
    g_th_up = (rc == 0);
    return g_th_up;
}
}  // namespace
bool thread_start(void (*body)(void)) { g_body = body; return th_spawn(); }
bool thread_retry() { return g_body ? th_spawn() : false; }
bool thread_stop() {
    if (!g_th_up) return true;
    hw::integration::g_stop.store(true);   // libera write() si espera contrapresión
    pthread_join(g_th, nullptr);
    g_th_up = false;
    return true;
}
bool wav_check(const char*, WavStats*, char*, unsigned, int) { return false; }

}}  // namespace d2rt::audio
