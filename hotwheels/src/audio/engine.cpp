// src/audio/engine.cpp — implementación de la integración de audio (HW-018).
#include "audio/engine.h"

#include "audio/output_contract.h"
#include "audio/tap_sink.h"
#include "audio/wav.h"
#if defined(__vita__)
#include "audio/vita_sink.h"
#endif

#include <utility>

namespace hw::audio {

namespace {
// Tope de la captura de consola (SPEC-048): deja un WAV válido aunque el
// harness mate el proceso a los 20 s.
constexpr int kTapSeconds = 20;

class DiscardSink final : public Sink {
public:
    bool open(int, int) override { return true; }
    void write(const int16_t*, int) override {}
    void close() override {}
    const char* name() const override { return "discard"; }
};
}  // namespace

bool Engine::start(const EngineConfig& cfg) {
    out_rate_ = cfg.out_rate;
    // El puerto consume bloques de 64 frames: el grano se alinea con la misma
    // función que usa el sink, así el motor produce exactamente lo que el
    // puerto reproduce (si no, el puerto oiría frames de relleno y la mezcla
    // avanzaría más despacio que el audio).
    grain_ = output_block_frames(cfg.grain);
    music_.set_resolver(cfg.music_resolver);
    if (cfg.vita) {
#if defined(__vita__)
        AudioThreadSpec spec;
        spec.grain = grain_;
        spec.step = &Engine::thread_step;
        spec.ctx = this;
        sink_.reset(make_vita_sink(spec));
        if (!sink_) return false;
        // HW_WAV=1 en consola: el sink es sceAudioOut y no escribe archivo, así
        // que copiamos el mismo bloque que se le entrega al driver.
        if (!cfg.wav_path.empty())
            sink_.reset(new TapSink(std::move(sink_), cfg.wav_path, kTapSeconds * out_rate_));
#else
        return false;  // sink sceAudioOut sólo existe en el build Vita
#endif
    } else {
        if (cfg.wav_path.empty()) sink_.reset(new DiscardSink());
        else sink_.reset(new WavWriter(cfg.wav_path));
    }
    if (!sink_) return false;
    return sink_->open(out_rate_, kChannels);
}

void Engine::stop() {
    if (sink_) {
        sink_->close();
        sink_.reset();
    }
}

void Engine::pump(int frames) {
    if (!sink_ || frames <= 0) return;
    scratch_.resize((size_t)frames * 2);
    mixer_.mix(scratch_.data(), frames);
    sink_->write(scratch_.data(), frames);
}

void Engine::thread_step(void* ctx) {
    auto* e = static_cast<Engine*>(ctx);
    e->pump(e->grain_);
}

}  // namespace hw::audio
