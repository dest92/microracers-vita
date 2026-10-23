// src/audio/wav.cpp — implementación WAV PCM16 (HW-018).
#include "audio/wav.h"

#include <cstring>
#include <vector>

namespace hw::audio {

namespace {
void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
}  // namespace

// ---- WavWriter -------------------------------------------------------------

bool WavWriter::open(int rate, int ch) {
    close();
    rate_ = rate;
    ch_ = ch;
    f_ = std::fopen(path_.c_str(), "wb");
    if (!f_) return false;
    data_ = 0;
    last_refresh_ = 0;
    header(0);
    return true;
}

void WavWriter::write(const int16_t* pcm, int frames) {
    if (!f_ || frames <= 0) return;
    const size_t n = (size_t)frames * (size_t)ch_;
    if (std::fwrite(pcm, sizeof(int16_t), n, f_) != n) return;
    data_ += (uint32_t)(n * sizeof(int16_t));
    if (data_ - last_refresh_ >= 64u * 1024u) refresh();
}

void WavWriter::close() {
    if (!f_) return;
    refresh();
    std::fclose(f_);
    f_ = nullptr;
}

void WavWriter::header(uint32_t data_bytes) {
    uint8_t h[44];
    std::memcpy(h, "RIFF", 4);        put32(h + 4, 36 + data_bytes);
    std::memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    put16(h + 20, 1); put16(h + 22, (uint16_t)ch_);
    put32(h + 24, (uint32_t)rate_);
    put32(h + 28, (uint32_t)(rate_ * ch_ * 2));
    put16(h + 32, (uint16_t)(ch_ * 2)); put16(h + 34, 16);
    std::memcpy(h + 36, "data", 4);    put32(h + 40, data_bytes);
    std::fwrite(h, 1, sizeof h, f_);
}

void WavWriter::refresh() {
    const long cur = std::ftell(f_);
    if (cur < 0) return;
    std::fseek(f_, 0, SEEK_SET);
    header(data_);
    std::fseek(f_, cur, SEEK_SET);
    std::fflush(f_);
    last_refresh_ = data_;
}

// ---- WavFileSource ---------------------------------------------------------

bool WavFileSource::open(const std::string& path) {
    close();
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) return false;
    uint8_t hdr[12];
    if (std::fread(hdr, 1, 12, f_) != 12 || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
        close();
        return false;
    }
    rate_ = 0; ch_ = 0; data_frames_ = 0; data_off_ = 0;
    uint16_t bits = 0, fmt = 0;
    for (;;) {
        uint8_t ch8[8];
        if (std::fread(ch8, 1, 8, f_) != 8) break;
        const uint32_t size = get32(ch8 + 4);
        if (std::memcmp(ch8, "fmt ", 4) == 0) {
            // Leer a lo sumo lo que necesitamos; descartar el resto del chunk.
            // (fread directo con `size` desbordaba si un WAV traía fmt > 40 B.)
            const size_t toread = size < 40 ? size : 40;
            std::vector<uint8_t> b(toread, 0);
            if (toread && std::fread(b.data(), 1, toread, f_) != toread) break;
            if (size > toread) std::fseek(f_, (long)(size - toread), SEEK_CUR);
            if (size >= 16) { fmt = get16(b.data()); ch_ = get16(b.data() + 2); rate_ = (int)get32(b.data() + 4); bits = get16(b.data() + 14); }
            if (size & 1) std::fseek(f_, 1, SEEK_CUR);
        } else if (std::memcmp(ch8, "data", 4) == 0) {
            data_off_ = std::ftell(f_);
            data_frames_ = (ch_ > 0) ? (uint64_t)size / ((uint64_t)ch_ * 2u) : 0;
            break;
        } else {
            std::fseek(f_, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    if (rate_ <= 0 || (ch_ != 1 && ch_ != 2) || bits != 16 || fmt != 1 || data_frames_ == 0) {
        close();
        return false;
    }
    pos_ = 0; loop_start_ = 0;
    return true;
}

void WavFileSource::close() {
    if (f_) { std::fclose(f_); f_ = nullptr; }
}

bool WavFileSource::seek_frame(uint64_t frame) {
    if (!f_ || frame > data_frames_) return false;
    if (std::fseek(f_, data_off_ + (long)(frame * (uint64_t)ch_ * 2u), SEEK_SET) != 0) return false;
    pos_ = frame;
    return true;
}

int WavFileSource::read(int16_t* out, int frames) {
    if (!f_) return 0;
    int produced = 0;
    int16_t tmp[4096 * 2];
    while (produced < frames) {
        if (pos_ >= data_frames_) {
            if (loop_ && data_frames_ > 0) {
                if (!seek_frame(loop_start_)) break;
            } else {
                break;
            }
        }
        int want = frames - produced;
        if ((uint64_t)want > data_frames_ - pos_) want = (int)(data_frames_ - pos_);
        if (want > 4096) want = 4096;
        if (want <= 0) break;
        const size_t nread = std::fread(tmp, sizeof(int16_t), (size_t)want * (size_t)ch_, f_);
        const int got = (int)(nread / (size_t)ch_);
        if (got <= 0) break;
        for (int i = 0; i < got; ++i) {
            if (ch_ == 2) {
                out[(size_t)(produced + i) * 2] = tmp[(size_t)i * 2];
                out[(size_t)(produced + i) * 2 + 1] = tmp[(size_t)i * 2 + 1];
            } else {
                out[(size_t)(produced + i) * 2] = tmp[i];
                out[(size_t)(produced + i) * 2 + 1] = tmp[i];
            }
        }
        pos_ += (uint64_t)got;
        produced += got;
    }
    return produced;
}

}  // namespace hw::audio
