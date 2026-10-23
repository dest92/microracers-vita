// src/audio/music.cpp — reproductor MCI/CD-audio (HW-018).
#include "audio/music.h"
#include "diag/thread_cpu.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace hw::audio {

MusicPlayer::~MusicPlayer() { stop(); }

void MusicPlayer::stop_decoder() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        decoder_stop_ = true;
        playing_ = false;
        paused_ = false;
    }
    cv_.notify_all();
    if (decoder_started_) {
        pthread_join(decoder_, nullptr);
        decoder_started_ = false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        src_.reset();
        ring_read_ = ring_write_ = ring_frames_ = 0;
        decoder_stop_ = false;
        decoder_eof_ = false;
        rate_.store(kSampleRate, std::memory_order_relaxed);
    }
}

void MusicPlayer::play(int track, const std::string& path, uint32_t from_tmsf, uint32_t to_tmsf) {
    std::lock_guard<std::mutex> command_lock(command_mutex_);
    stop_decoder();
    Resolver resolver;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        track_ = track;
        resolver = resolver_;
    }
    if (path.empty()) return;

    const std::string host = resolver ? resolver(path) : path;
    if (host.empty()) return;
    auto source = std::unique_ptr<WavFileSource>(new WavFileSource());
    if (!source->open(host)) return;

    source->set_loop(to_tmsf == 0);
    using namespace hw::winmm;
    const double start_s = MCI_TMSF_MINUTE(from_tmsf) * 60.0
                         + MCI_TMSF_SECOND(from_tmsf)
                         + MCI_TMSF_FRAME(from_tmsf) / 75.0;
    uint64_t start = static_cast<uint64_t>(start_s * source->rate());
    if (start >= source->total_frames()) start = 0;
    source->seek_frame(start);
    source->set_loop_start(start);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rate_.store(source->rate(), std::memory_order_relaxed);
        src_ = std::move(source);
        playing_ = true;
        decoder_eof_ = false;
    }
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        stop_decoder();
        return;
    }
    const int stack_rc = pthread_attr_setstacksize(&attr, 64 * 1024);
    const int create_rc = stack_rc == 0 ? pthread_create(&decoder_, &attr, &MusicPlayer::decoder_entry, this) : stack_rc;
    pthread_attr_destroy(&attr);
    if (create_rc != 0) {
        stop_decoder();
        return;
    }
    decoder_started_ = true;

    // MCI_PLAY no retorna hasta que el primer bloque está disponible: evita
    // empezar la reproducción con un underrun inicial del ring.
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return ring_frames_ != 0 || decoder_eof_ || decoder_stop_; });
}

void MusicPlayer::decode_loop() {
    int16_t decoded[kDecodeFrames * 2];
    for (;;) {
        size_t want;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return decoder_stop_ || (!paused_ && ring_frames_ < kRingFrames); });
            if (decoder_stop_) return;
            want = std::min(kRingFrames - ring_frames_, static_cast<size_t>(kDecodeFrames));
        }

        // src_ pertenece sólo a este hilo desde que arranca hasta join en stop_decoder.
        const int got = src_->read(decoded, static_cast<int>(want));
        std::unique_lock<std::mutex> lock(mutex_);
        if (decoder_stop_) return;
        if (got <= 0) {
            decoder_eof_ = true;
            cv_.notify_all();
            return;
        }
        const size_t count = static_cast<size_t>(got);
        const size_t first = std::min(count, kRingFrames - ring_write_);
        std::memcpy(ring_.data() + ring_write_ * 2, decoded, first * 2 * sizeof(int16_t));
        if (first < count)
            std::memcpy(ring_.data(), decoded + first * 2, (count - first) * 2 * sizeof(int16_t));
        ring_write_ = (ring_write_ + count) % kRingFrames;
        ring_frames_ += count;
        lock.unlock();
        cv_.notify_all();
    }
}

void* MusicPlayer::decoder_entry(void* ctx) {
    hw::diag::ThreadRegistration registration(hw::diag::ThreadRole::Music);
    static_cast<MusicPlayer*>(ctx)->decode_loop();
    return nullptr;
}

void MusicPlayer::stop() {
    std::lock_guard<std::mutex> command_lock(command_mutex_);
    stop_decoder();
}

void MusicPlayer::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (playing_) paused_ = true;
    cv_.notify_all();
}

int MusicPlayer::read(int16_t* out, int frames) {
    if (!out || frames <= 0) return 0;
    std::unique_lock<std::mutex> lock(mutex_);
    if (!playing_ || paused_ || !ring_frames_) return 0;
    const size_t count = std::min(static_cast<size_t>(frames), ring_frames_);
    const size_t first = std::min(count, kRingFrames - ring_read_);
    std::memcpy(out, ring_.data() + ring_read_ * 2, first * 2 * sizeof(int16_t));
    if (first < count)
        std::memcpy(out + first * 2, ring_.data(), (count - first) * 2 * sizeof(int16_t));
    ring_read_ = (ring_read_ + count) % kRingFrames;
    ring_frames_ -= count;
    lock.unlock();
    cv_.notify_all();
    return static_cast<int>(count);
}

int MusicPlayer::rate() const { return rate_.load(std::memory_order_relaxed); }

bool MusicPlayer::playing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playing_;
}

bool MusicPlayer::paused() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

int MusicPlayer::track() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return track_;
}

}  // namespace hw::audio
