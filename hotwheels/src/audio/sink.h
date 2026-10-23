// src/audio/sink.h — salida del backend de audio (HW-018).
#pragma once
#include <cstdint>

namespace hw::audio {

// Destino del stream mezclado (48000 Hz S16 estéreo).
//
// Contrato de ciclo de vida (lo usa TapSink): un sink puede arrancar un escritor
// dentro de open(), que empiece a llamar write() antes de que open() retorne; y
// close() tiene que haber detenido a ese escritor (join) antes de retornar. Así
// la vida del sink queda contenida entre open() y close().
struct Sink {
    virtual ~Sink() = default;
    virtual bool open(int rate, int ch) = 0;
    virtual void write(const int16_t* pcm, int frames) = 0;
    virtual void close() = 0;
    virtual const char* name() const = 0;
};

}  // namespace hw::audio
