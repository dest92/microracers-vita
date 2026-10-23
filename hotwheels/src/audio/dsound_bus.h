// Compatibilidad temporal para consumidores de audio de Hot Wheels.
#pragma once
#include "audio/pcm.h"
#include "../../../winx86-dx/include/winx86-dx/audio/dsound_bus.h"

namespace hw::audio {
using ::winx86::dx::audio::DsoundBus;
using ::winx86::dx::audio::DsoundBusConfig;
}
