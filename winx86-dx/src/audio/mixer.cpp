// Portable stereo PCM mixer.
#include "../../include/winx86-dx/audio/mixer.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace winx86::dx::audio {

void Mixer::add(PcmSource* source, float gain) {
    if (!source) return;
    SourceState state;
    state.source = source;
    state.gain = gain;
    state.buffer.assign(static_cast<size_t>(kBlockFrames) * 2, 0);
    sources_.push_back(std::move(state));
}

void Mixer::clear() { sources_.clear(); }

bool Mixer::more(SourceState& source) {
    if (source.current > 0) {
        // A downsample step may skip beyond this entire pull. Keep that
        // debt for the next pull; a negative fill would write before buffer.
        const int consumed = std::min(source.current, source.fill);
        const int remaining = source.fill - consumed;
        if (remaining > 0) {
            std::memmove(source.buffer.data(), source.buffer.data() + static_cast<size_t>(consumed) * 2,
                         static_cast<size_t>(remaining) * 2 * sizeof(int16_t));
        }
        source.fill = remaining;
        source.current -= consumed;
    }
    const int space = kBlockFrames - source.fill;
    if (space <= 0) return false;
    const int got = source.source->read(source.buffer.data() + static_cast<size_t>(source.fill) * 2, space);
    if (got <= 0) {
        source.retry_blocked = true;
        return false;
    }
    source.fill += got;
    return true;
}

double Mixer::sample(const SourceState& source, int channel) const {
    const double a = source.buffer[static_cast<size_t>(source.current) * 2 + channel];
    const double b = source.current + 1 < source.fill
        ? source.buffer[static_cast<size_t>(source.current + 1) * 2 + channel] : a;
    return a + (b - a) * source.fraction;
}

int Mixer::mix(int16_t* out, int frames) {
    for (auto& source : sources_) source.retry_blocked = false;
    for (int frame = 0; frame < frames; ++frame) {
        int left = 0, right = 0;
        for (auto& source : sources_) {
            while (source.current >= source.fill) {
                if (source.retry_blocked || !more(source)) break;
            }
            if (source.current < source.fill) {
                // Linear interpolation needs the following frame even when
                // the current one is still available. Preserve it across
                // pull boundaries; otherwise every boundary inserts a hold.
                if (source.fraction > 0.0 && source.current + 1 >= source.fill
                    && !source.retry_blocked) more(source);
                left += static_cast<int>(sample(source, 0) * source.gain);
                right += static_cast<int>(sample(source, 1) * source.gain);
                const double step = static_cast<double>(source.source->rate()) / output_rate_;
                source.fraction += step;
                while (source.fraction >= 1.0) {
                    source.fraction -= 1.0;
                    ++source.current;
                }
            }
        }
        out[static_cast<size_t>(frame) * 2] = clamp_s16(left);
        out[static_cast<size_t>(frame) * 2 + 1] = clamp_s16(right);
    }
    return frames;
}

}  // namespace winx86::dx::audio
