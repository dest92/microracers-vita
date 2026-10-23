// Compatibilidad temporal para consumidores de audio de Hot Wheels.
#pragma once
#include "../../../winx86-dx/include/winx86-dx/audio/pcm.h"

namespace hw::audio {
using ::winx86::dx::audio::kChannels;
using ::winx86::dx::audio::kSampleRate;
using ::winx86::dx::audio::clamp_s16;
using ::winx86::dx::audio::PcmSource;
}
