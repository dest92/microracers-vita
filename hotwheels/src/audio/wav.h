// src/audio/wav.h — WAV PCM16: escritura (sink host) y lectura streaming (HW-018).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#include "audio/pcm.h"
#include "audio/sink.h"

namespace hw::audio {

// Escribe un RIFF/WAVE PCM 16-bit. Refresca el header cada 64 KiB para que un
// run cortado (timeout/crash) deje un archivo legible.
class WavWriter : public Sink {
public:
    explicit WavWriter(std::string path) : path_(std::move(path)) {}
    ~WavWriter() override { close(); }

    bool open(int rate, int ch) override;
    void write(const int16_t* pcm, int frames) override;
    void close() override;
    const char* name() const override { return "wav"; }

private:
    void header(uint32_t data_bytes);
    void refresh();

    std::string path_;
    FILE* f_ = nullptr;
    int rate_ = 48000, ch_ = 2;
    uint32_t data_ = 0, last_refresh_ = 0;
};

// Lee un WAV PCM16 (1–2 canales) y lo entrega streameando como PcmSource
// estéreo. Soporta loop y seek por frame (para MCI TMSF).
class WavFileSource : public PcmSource {
public:
    ~WavFileSource() override { close(); }

    bool open(const std::string& path);
    void close();
    int read(int16_t* out, int frames) override;
    int rate() const override { return rate_; }

    int channels() const { return ch_; }
    uint64_t total_frames() const { return data_frames_; }
    bool seek_frame(uint64_t frame);
    void set_loop(bool loop) { loop_ = loop; }
    void set_loop_start(uint64_t frame) { loop_start_ = frame; }

private:
    FILE* f_ = nullptr;
    long data_off_ = 0;
    uint64_t data_frames_ = 0;
    uint64_t pos_ = 0;       // frame actual
    uint64_t loop_start_ = 0;
    int rate_ = 48000, ch_ = 2;
    bool loop_ = false;
};

}  // namespace hw::audio
