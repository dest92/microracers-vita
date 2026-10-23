// src/audio/tap_sink.h — decorador de Sink que además vuelca el PCM a un WAV
// (SPEC-048 CA10/CA11, Fase 2).
//
// En consola `HW_WAV=1` pide un WAV, pero el sink elegido es sceAudioOut, que no
// escribe archivos. Este decorador envuelve al sink real y copia el PCM que
// recibe —el mismo bloque que el Engine entrega al dispositivo— a un WAV, con
// tope de frames para que el archivo quede válido aunque el harness mate el
// proceso. El sink real no se recorta: sigue recibiendo todo.
#pragma once

#include <memory>
#include <string>

#include "audio/sink.h"
#include "audio/wav.h"

namespace hw::audio {

class TapSink : public Sink {
public:
    // `inner` no puede ser nulo. limit_frames > 0: tope de frames a capturar (el
    // WAV se cierra al llegar); 0: sin tope.
    TapSink(std::unique_ptr<Sink> inner, std::string path, int limit_frames)
        : inner_(std::move(inner)),
          path_(std::move(path)),
          left_(limit_frames > 0 ? limit_frames : -1) {}
    ~TapSink() override { close(); }

    TapSink(const TapSink&) = delete;
    TapSink& operator=(const TapSink&) = delete;

    bool open(int rate, int ch) override {
        // El tap se abre ANTES de arrancar el sink real: un sink puede crear su
        // hilo dentro de open() y empezar a llamar write() antes de retornar
        // (VitaSink hace exactamente eso), así que `tap_` tiene que estar listo
        // y no volver a mutar mientras el worker corre.
        tap_.reset(new WavWriter(path_));
        if (!tap_->open(rate, ch)) tap_.reset();  // sin archivo: seguimos sin tap
        if (inner_->open(rate, ch)) return true;
        tap_.reset();  // el worker no arrancó: nadie mira `tap_`
        return false;
    }

    void write(const int16_t* pcm, int frames) override {
        inner_->write(pcm, frames);
        if (!tap_ || frames <= 0) return;
        int n = frames;
        if (left_ >= 0 && n > left_) n = left_;
        tap_->write(pcm, n);
        if (left_ >= 0) {
            left_ -= n;
            if (left_ == 0) { tap_->close(); tap_.reset(); }
        }
    }

    void close() override {
        // Al revés que en open: primero se detiene el sink real (baja su bandera
        // y hace join), y recién después se cierra el tap. Cerrar el tap antes
        // dejaría al worker escribiendo sobre un `tap_` ya destruido.
        inner_->close();
        if (tap_) { tap_->close(); tap_.reset(); }
    }

    const char* name() const override { return inner_->name(); }

private:
    std::unique_ptr<Sink> inner_;
    std::unique_ptr<WavWriter> tap_;
    std::string path_;
    int left_ = -1;  // frames restantes hasta el tope; <0 = sin tope
};

}  // namespace hw::audio
