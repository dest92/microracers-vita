// src/audio/ds_bridge.h — seam portable del backend DirectSound (HW-020 audio).
//
// El sink de ds_emul (winx86) entrega PCM de efectos; esta capa lo mete en el
// DsoundBus del Engine de HW-018, lo mezcla con la música de HW-015 y lo vuelca
// al device (host: WAV; Vita: hilo sceAudioOut). La parte que conoce winx86 vive
// en src/integration/dsound_bridge.cpp; esto es 100% host-testeable.
#pragma once
#include <cstdint>

#include "audio/engine.h"
#include "winmm/winmm.h"

namespace hw::audio {

// Arranca el Engine global (una sola vez). false = no abrió sink.
bool ds_backend_start(const EngineConfig& cfg);
bool ds_backend_running();
Engine* ds_backend_engine();
hw::winmm::MusicSink* ds_backend_music();

// PCM de efectos @src_rate. En host además mezcla+vuelca (WAV); en Vita sólo
// encola (el hilo del Engine es el que saca por sceAudioOut).
void ds_backend_feed(const int16_t* pcm, int frames, int src_rate);
void ds_backend_stop();

}  // namespace hw::audio
