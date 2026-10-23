// src/audio/output_contract.h — contrato de bloques de salida sceAudioOut (SPEC-048 CA11).
//
// Mecanismo (psp2/audioout.h + sink de winx86 src/platform/vita_audio.cpp):
//  1. `sceAudioOutOpenPort(type, len, freq, mode)` exige `len` entre
//     SCE_AUDIO_MIN_LEN (64) y SCE_AUDIO_MAX_LEN, múltiplo de 64. El len se fija
//     al abrir el puerto y `sceAudioOutOutput` consume SIEMPRE ese len, sin
//     importar cuántos frames válidos tenga el buffer.
//  2. Si el buffer trae menos de `len` frames válidos, el driver lee la cola
//     vieja (el final del grano anterior): hay que dejar esa cola en cero
//     (observado y documentado en el sink de winx86).
//  3. El SDK **no** documenta un requisito de alineación del puntero ni el
//     instante exacto en que el puntero puede reutilizarse. La copia a 64 B
//     (múltiplo de la línea L1 de 32 B de la plataforma) es DEFENSIVA: el origen
//     es un std::vector que sólo garantiza 16 B. Lo que hace seguro reutilizar el
//     bloque es que `sceAudioOutOutput` es bloqueante; el resto en cola se mide
//     con `sceAudioOutGetRestSample` (ver vita_sink.cpp).
//
// Con `frames == len` la copia no cuesta memset; con `frames < len` rellena con
// cero. Sin asignación dinámica: OutputBlocks vive en el objeto del sink y la
// ruta de audio (hilo realtime) nunca llama a malloc.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hw::audio {

// Múltiplo de frames exigido por el driver.
inline constexpr int kOutputBlockAlignment = 64;

// Frames del bloque de salida: múltiplo de 64 (lo que pide el driver) y al
// menos 64. El dueño del grano (Engine) y el sink usan ESTA función, así el
// motor produce exactamente lo que el puerto consume: si no, el puerto
// reproduciría frames de relleno y la mezcla avanzaría más despacio que el
// audio (drift del 2,4 % con un grano de 500).
inline int output_block_frames(int frames) {
    if (frames <= 0) return 0;
    const int aligned = (frames + kOutputBlockAlignment - 1) / kOutputBlockAlignment
                      * kOutputBlockAlignment;
    return aligned < kOutputBlockAlignment ? kOutputBlockAlignment : aligned;
}

// Techo de frames por bloque: es también el techo de grano del sink Vita. Un
// puerto más ancho no se puede servir sin desbordar estos buffers, así que se
// rechaza al abrir en vez de descubrir el desborde en consola.
inline constexpr int kOutputBlockMaxFrames = 2048;

// sceAudioOutOutput devuelve >=0 si aceptó el bloque; <0 es error.
inline bool output_succeeded(int rc) { return rc >= 0; }

// Un bloque válido es no vacío, acotado y múltiplo de 64 frames.
inline bool valid_output_block(int frames) {
    return frames > 0 && frames <= kOutputBlockMaxFrames
        && frames % kOutputBlockAlignment == 0;
}

// Pingpong de bloques alineados a 64 B. Single-consumer (lo usa sólo el hilo de
// audio), sin locks. Dos buffers y no uno: el driver podría retener el bloque
// recién encolado; alternar deja escrito el anterior hasta dos envíos después.
class OutputBlocks {
public:
    OutputBlocks() = default;
    OutputBlocks(const OutputBlocks&) = delete;
    OutputBlocks& operator=(const OutputBlocks&) = delete;

    // Copia `frames` frames estéreo desde `src` al buffer que toca del pingpong,
    // rellena con cero hasta `block` frames y devuelve el buffer (alineado a 64 B).
    // Devuelve nullptr si las dimensiones son inválidas. El resultado vale hasta
    // la próxima llamada a prepare().
    int16_t* prepare(const int16_t* src, int frames, int block) {
        if (!src || frames <= 0 || frames > block || !valid_output_block(block))
            return nullptr;
        int16_t* dst = buffers_[next_];
        const size_t copied = static_cast<size_t>(frames) * 2;
        const size_t padded = static_cast<size_t>(block) * 2;
        std::memcpy(dst, src, copied * sizeof(int16_t));
        if (padded > copied) std::memset(dst + copied, 0, (padded - copied) * sizeof(int16_t));
        next_ ^= 1;
        return dst;
    }

private:
    // Cada fila mide un múltiplo de 64 B, así ambas quedan alineadas con el
    // alignas del arreglo.
    alignas(kOutputBlockAlignment) int16_t buffers_[2][kOutputBlockMaxFrames * 2] = {};
    int next_ = 0;
};

}  // namespace hw::audio
