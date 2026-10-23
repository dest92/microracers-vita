// src/audio/music.h — reproductor de música MCI/CD-audio (HW-018, integra HW-015).
#pragma once
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include "audio/pcm.h"
#include "audio/wav.h"
#include "winmm/winmm.h"

namespace hw::audio {

// Implementa hw::winmm::MusicSink reproduciendo los MUSIC\TrackN.wav con
// WavFileSource (streaming) en un productor separado y expone la señal por un
// ring acotado al Mixer. El consumidor nunca hace I/O de archivos.
class MusicPlayer : public hw::winmm::MusicSink, public PcmSource {
public:
    // Mapea "MUSIC\\TrackN.wav" a una ruta del host. Vacío => usa el path tal cual.
    using Resolver = std::function<std::string(const std::string& win_path)>;

    explicit MusicPlayer(Resolver resolver = {}) : resolver_(std::move(resolver)) {}
    ~MusicPlayer() override;

    void set_resolver(Resolver resolver) { std::lock_guard<std::mutex> lock(mutex_); resolver_ = std::move(resolver); }

    // hw::winmm::MusicSink
    void play(int track, const std::string& path, uint32_t from_tmsf, uint32_t to_tmsf) override;
    void stop() override;
    void pause() override;

    // hw::audio::PcmSource
    int read(int16_t* out, int frames) override;
    int rate() const override;

    bool playing() const;
    bool paused() const;
    int track() const;

private:
    static constexpr size_t kRingFrames = 8192;
    static constexpr int kDecodeFrames = 1024;
    void stop_decoder();
    void decode_loop();
    static void* decoder_entry(void* ctx);

    Resolver resolver_;
    mutable std::mutex mutex_;
    std::mutex command_mutex_;
    std::condition_variable cv_;
    std::unique_ptr<WavFileSource> src_;
    std::vector<int16_t> ring_ = std::vector<int16_t>(kRingFrames * 2);
    pthread_t decoder_{};
    bool decoder_started_ = false;
    size_t ring_read_ = 0, ring_write_ = 0, ring_frames_ = 0;
    bool decoder_stop_ = false, decoder_eof_ = false;
    std::atomic<int> rate_{kSampleRate};
    bool playing_ = false, paused_ = false;
    int track_ = 0;
};

}  // namespace hw::audio
