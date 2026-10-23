// Pull interface for interleaved stereo signed 16-bit PCM.
#pragma once

#include <cstdint>

namespace winx86::dx::audio {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

inline int16_t clamp_s16(int value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

struct PcmSource {
    virtual ~PcmSource() = default;
    virtual int read(int16_t* out, int frames) = 0;
    virtual int rate() const = 0;
};

}  // namespace winx86::dx::audio
