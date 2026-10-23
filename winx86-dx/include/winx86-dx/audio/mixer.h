// Deterministic pull mixer with per-source linear resampling.
#pragma once

#include "pcm.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace winx86::dx::audio {

class Mixer {
public:
    static constexpr int kBlockFrames = 4096;

    explicit Mixer(int output_rate = kSampleRate)
        : output_rate_(output_rate > 0 ? output_rate : kSampleRate) {}
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    Mixer(Mixer&&) = delete;
    Mixer& operator=(Mixer&&) = delete;

    // Single-consumer object with no internal lock. Source is borrowed and
    // must outlive its registration in this mixer; clear() unregisters all.
    void add(PcmSource* source, float gain = 1.0f);
    void clear();
    int sources() const { return static_cast<int>(sources_.size()); }
    int output_rate() const { return output_rate_; }

    // Writes stereo S16 frames and returns the requested frame count.
    int mix(int16_t* out, int frames);

private:
    struct SourceState {
        PcmSource* source = nullptr;
        float gain = 1.0f;
        std::vector<int16_t> buffer;
        int fill = 0;
        int current = 0;
        double fraction = 0.0;
        bool retry_blocked = false;
    };

    bool more(SourceState& source);
    double sample(const SourceState& source, int channel) const;

    int output_rate_;
    std::vector<SourceState> sources_;
};

}  // namespace winx86::dx::audio
