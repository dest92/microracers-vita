// src/audio/engine.h — integración del backend de audio (HW-018, spec 009).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/dsound_bus.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/sink.h"
#include "winmm/winmm.h"

namespace hw::audio {

struct EngineConfig {
    int out_rate = kSampleRate;   // 48000 (única tasa del MAIN port)
    std::string wav_path;         // host: destino de la evidencia WAV
    bool vita = false;            // true: usa el sink sceAudioOut (sólo __vita__)
    int grain = 512;              // frames por grano del hilo Vita
    MusicPlayer::Resolver music_resolver;  // mapea MUSIC\TrackN.wav -> host
};

// Une el bus de efectos (DsoundBus), el reproductor de música y el sink de
// salida. En host no arranca hilo: `pump()` es empujado por el reloj del guest
// (o por los tests). En Vita, el hilo `hw_audio` llama a `pump(grain)` en loop,
// que bloquea en sceAudioOutOutput — nunca el hilo del guest.
class Engine {
public:
    Engine() {
        mixer_.add(&ds_, 1.0f);
        mixer_.add(&music_, 1.0f);
    }
    ~Engine() { stop(); }

    bool start(const EngineConfig& cfg);
    void stop();
    void pump(int frames);  // host: mezcla y escribe `frames`

    hw::winmm::MusicSink* music() { return &music_; }
    DsoundBus& dsound() { return ds_; }
    int out_rate() const { return out_rate_; }
    uint64_t underruns() const { return ds_.underruns(); }

private:
    static void thread_step(void* ctx);  // hilo Vita: pump(grain_)

    DsoundBus ds_;
    MusicPlayer music_;
    Mixer mixer_;
    std::unique_ptr<Sink> sink_;
    int out_rate_ = kSampleRate;
    int grain_ = 512;
    std::vector<int16_t> scratch_;
};

}  // namespace hw::audio
