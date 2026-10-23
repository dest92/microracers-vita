// tests/test_audio_tap_lifecycle.cpp — SPEC-048 (review 7631274, P1): el decorador
// TapSink no puede arrancar al worker antes de estar listo ni cerrar su estado
// compartido antes de detenerlo.
//
// Modelo fiel al VitaSink: su `open()` crea y deja correr el pthread ANTES de
// retornar, así que el feeder puede llamar a `TapSink::write` mientras open()
// todavía está construyendo `tap_`; y en close(), el feeder escribe hasta que el
// inner hace join. Con el orden viejo (inner->open primero, tap_ después; tap_
// destruido antes de inner->close) los bloques de borde se pierden. Este test lo
// detecta de forma determinista: no depende de timing.
#include "audio/tap_sink.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

using namespace hw::audio;

namespace {
// Sink de prueba que "alimenta" PCM durante open() y durante close() — igual que
// el hilo del VitaSink, que ya corre cuando su open() retorna y escribe hasta que
// close() hace join.
class FeederLikeSink : public Sink {
public:
    std::function<void()> during_open;
    std::function<void()> during_close;
    std::vector<int16_t> got;

    bool open(int, int) override {
        if (during_open) during_open();
        return true;
    }
    void write(const int16_t* pcm, int frames) override {
        got.insert(got.end(), pcm, pcm + (size_t)frames * 2);
    }
    void close() override {
        if (during_close) during_close();
    }
    const char* name() const override { return "feeder-like"; }
};
}  // namespace

int main() {
    const char* path = "build/test_audio_tap_lifecycle.wav";
    std::remove(path);

    auto inner = std::unique_ptr<FeederLikeSink>(new FeederLikeSink());
    FeederLikeSink* raw = inner.get();
    TapSink tap(std::move(inner), path, 0);  // sin tope: aísla el ciclo de vida

    // El worker escribe mientras open() todavía no retornó (arranca dentro del
    // inner->open) y otro bloque mientras close() está en curso (antes del join).
    raw->during_open = [&] { const int16_t b[4] = {100, 101, 102, 103}; tap.write(b, 2); };
    raw->during_close = [&] { const int16_t b[4] = {200, 201, 202, 203}; tap.write(b, 2); };

    assert(tap.open(48000, 2));
    const int16_t mid[4] = {10, 11, 12, 13};
    tap.write(mid, 2);
    tap.close();

    // Los tres bloques tienen que estar en el WAV: el de borde de open, el del
    // medio y el de borde de close.
    WavFileSource src;
    assert(src.open(path));
    assert(src.total_frames() == 6);
    std::vector<int16_t> out(12, 0);
    assert(src.read(out.data(), 6) == 6);
    const int16_t expect[12] = {100, 101, 102, 103, 10, 11, 12, 13, 200, 201, 202, 203};
    for (int i = 0; i < 12; ++i) {
        if (out[i] != expect[i]) {
            std::printf("FAIL muestra %d: %d != %d (borde de open/close perdido)\n", i, out[i], expect[i]);
            return 1;
        }
    }
    std::remove(path);
    std::puts("AUDIO-TAP-LIFECYCLE: PASS");
}
