// src/audio/ds_bridge.cpp — implementación del seam (HW-020 audio).
#include "audio/ds_bridge.h"

#include <string>

namespace hw::audio {
namespace {
Engine* g_engine = nullptr;
bool g_vita = false;
int g_out = kSampleRate;

// Sink de música ESTABLE: se puede entregar al MCI (HW-015) antes de que el
// Engine exista (el Engine arranca recién en DirectSoundCreate). Reenvía cuando
// está listo y recuerda el último play para reproducirlo al arrancar.
class MusicProxy : public hw::winmm::MusicSink {
public:
    void play(int track, const std::string& path, uint32_t from_tmsf, uint32_t to_tmsf) override {
        if (auto* m = (g_engine ? g_engine->music() : nullptr)) { m->play(track, path, from_tmsf, to_tmsf); return; }
        track_ = track; path_ = path; from_ = from_tmsf; to_ = to_tmsf; pending_ = true;
    }
    void stop() override {
        if (auto* m = (g_engine ? g_engine->music() : nullptr)) m->stop();
        pending_ = false;
    }
    void pause() override {
        if (auto* m = (g_engine ? g_engine->music() : nullptr)) m->pause();
    }

    // Reenvía el último play guardado (lo llama ds_backend_start).
    void replay_if_pending() {
        if (!pending_) return;
        pending_ = false;
        if (auto* m = (g_engine ? g_engine->music() : nullptr)) m->play(track_, path_, from_, to_);
    }

private:
    int track_ = 0;
    std::string path_;
    uint32_t from_ = 0, to_ = 0;
    bool pending_ = false;
};

MusicProxy g_music;
}  // namespace

bool ds_backend_start(const EngineConfig& cfg) {
    if (g_engine) return true;
    Engine* e = new Engine();
    if (!e->start(cfg)) { delete e; return false; }
    g_engine = e;
    g_vita = cfg.vita;
    g_out = cfg.out_rate > 0 ? cfg.out_rate : kSampleRate;
    g_music.replay_if_pending();  // reproducir lo pedido antes de que existiera el Engine
    return true;
}

bool ds_backend_running() { return g_engine != nullptr; }
Engine* ds_backend_engine() { return g_engine; }
hw::winmm::MusicSink* ds_backend_music() { return &g_music; }  // estable: siempre válido

void ds_backend_feed(const int16_t* pcm, int frames, int src_rate) {
    if (!g_engine || !pcm || frames <= 0 || src_rate <= 0) return;
    g_engine->dsound().push(pcm, frames, src_rate);
    if (!g_vita) {
        const int out = (int)((int64_t)frames * g_out / src_rate);
        if (out > 0) g_engine->pump(out);
    }
}

void ds_backend_stop() {
    if (!g_engine) return;
    g_engine->stop();
    delete g_engine;
    g_engine = nullptr;
}

}  // namespace hw::audio
