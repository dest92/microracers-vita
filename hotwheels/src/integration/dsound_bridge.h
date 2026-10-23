// src/integration/dsound_bridge.h — instala ds_emul (winx86) sobre el backend
// de audio de HW-018 (spec 009/010). Punto de enganche para integrate.cpp.
#pragma once
#include <cstdint>
#include <string>

#include "winmm/winmm.h"

namespace d2rt { struct Cpu; class Bridge; }

namespace hw::integration {

struct DsoundConfig {
    bool vita = false;             // true: sceAudioOut; false: WAV host
    std::string wav_path;          // host: WAV de salida
    std::string write_root;        // HostOps.write_root (Host nullptr si vacío)
    std::string game_root;         // para resolver MUSIC\TrackN.wav
    uint32_t (*va_alloc)(uint32_t) = nullptr;  // arena guest para buffers PCM
    uint32_t (*tick_ms)(d2rt::Cpu&) = nullptr; // reloj guest (timeGetTime)
    int mix_rate = 22050;          // tasa del stream mezclado del juego
    int out_rate = 48000;          // salida del backend
    int grain = 512;               // frames por grano (Vita)
    bool self_paced = false;       // Vita: hilo propio de ds_emul + contrapresión (o env HW_AUDIO_SELF_PACED=1)
};

// Registra DirectSoundCreate y las vtables COM de ds_emul, enrutando su PCM al
// Engine de HW-018 (efectos + música). Llamar DESPUÉS de los stubs y de ddraw,
// ANTES de br.link() (el último registro gana). Devuelve false + `err` si no.
bool hw_register_dsound(d2rt::Bridge& br, d2rt::Cpu& cpu, const DsoundConfig& cfg,
                        std::string& err);

// Sink de música (HW-015) ya cableado al backend, para el MCI/cdaudio.
hw::winmm::MusicSink* hw_dsound_music();

}  // namespace hw::integration
