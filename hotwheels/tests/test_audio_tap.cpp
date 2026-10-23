// tests/test_audio_tap.cpp — SPEC-048 CA10/CA11 (Fase 2): TapSink copia el PCM
// que se entrega al sink real a un WAV, con tope de frames, sin alterar el
// stream del sink real (que sigue recibiendo todo).
#include "audio/tap_sink.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace hw::audio;

namespace {
// Sink de prueba: acumula lo que recibe.
class CollectSink : public Sink {
public:
    bool open(int, int) override { return true; }
    void write(const int16_t* pcm, int frames) override {
        got.insert(got.end(), pcm, pcm + (size_t)frames * 2);
    }
    void close() override {}
    const char* name() const override { return "collect"; }
    std::vector<int16_t> got;
};
}  // namespace

int main() {
    const char* path = "build/test_audio_tap.wav";
    std::remove(path);

    // Bloque de 1024 frames (2048 muestras) con una rampa reconocible.
    std::vector<int16_t> block(2048);
    for (int i = 0; i < 2048; ++i) block[i] = (int16_t)(i * 7 - 1500);

    // Tope 2048 frames: el tap corta ahí; el sink real recibe los 3072.
    {
        auto inner = std::unique_ptr<CollectSink>(new CollectSink());
        CollectSink* raw = inner.get();
        TapSink tap(std::move(inner), path, 2048);
        assert(tap.open(48000, 2));
        for (int b = 0; b < 3; ++b) tap.write(block.data(), 1024);
        assert(static_cast<int>(raw->got.size()) == 3 * 2048);
        tap.close();
    }
    {
        WavFileSource src;
        assert(src.open(path));
        assert(src.rate() == 48000 && src.channels() == 2);
        assert(src.total_frames() == 2048);
        std::vector<int16_t> out(2048 * 2, 0);
        assert(src.read(out.data(), 2048) == 2048);
        for (int i = 0; i < 2048 * 2; ++i) assert(out[i] == block[i % 2048]);
    }

    // Sin tope (0): todo el PCM (3072 frames = 6144 muestras).
    std::remove(path);
    {
        auto inner = std::unique_ptr<CollectSink>(new CollectSink());
        TapSink tap(std::move(inner), path, 0);
        assert(tap.open(48000, 2));
        for (int b = 0; b < 3; ++b) tap.write(block.data(), 1024);
        tap.close();
    }
    {
        WavFileSource src;
        assert(src.open(path));
        assert(src.total_frames() == 3072);
        std::vector<int16_t> out(3072 * 2, 0);
        assert(src.read(out.data(), 3072) == 3072);
        for (int i = 0; i < 3072 * 2; ++i) assert(out[i] == block[i % 2048]);
    }

    // Tope NO múltiplo del bloque (1500 frames con bloques de 1024): corta dentro
    // del segundo bloque. Sin el clamp por bloque, el 2º bloque entra entero y el
    // corte no se ve (era el mutante que sobrevivía).
    std::remove(path);
    {
        auto inner = std::unique_ptr<CollectSink>(new CollectSink());
        TapSink tap(std::move(inner), path, 1500);
        assert(tap.open(48000, 2));
        for (int b = 0; b < 3; ++b) tap.write(block.data(), 1024);
        tap.close();
    }
    {
        WavFileSource src;
        assert(src.open(path));
        assert(src.total_frames() == 1500);
        std::vector<int16_t> out(1500 * 2, 0);
        assert(src.read(out.data(), 1500) == 1500);
        for (int i = 0; i < 1500 * 2; ++i) assert(out[i] == block[i % 2048]);
    }

    // Fallo al abrir el WAV (directorio inexistente): el tap queda inactivo pero
    // el sink real sigue recibiendo todo.
    {
        auto inner = std::unique_ptr<CollectSink>(new CollectSink());
        CollectSink* raw = inner.get();
        TapSink tap(std::move(inner), "build/no-existe-dir/tap.wav", 0);
        assert(tap.open(48000, 2));
        tap.write(block.data(), 1024);
        assert(static_cast<int>(raw->got.size()) == 2048);
        tap.close();
    }

    std::remove(path);
    std::puts("AUDIO-TAP: PASS");
}
