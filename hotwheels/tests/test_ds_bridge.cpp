// tests/test_ds_bridge.cpp — HW-020 audio: seam ds_emul → Engine (spec 009/010).
// No usa winx86: verifica que alimentar el bus con PCM de efectos + música
// produce un WAV no vacío (el "out/*.wav" que pide la integración).
#include "audio/ds_bridge.h"
#include "audio/engine.h"
#include "audio/wav.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hw::audio;
static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m); ++g_fail; } } while (0)

static void make_wav(const std::string& path, int rate, double seconds, double hz) {
    const int frames = (int)(rate * seconds);
    std::vector<int16_t> pcm;
    for (int i = 0; i < frames; i++) {
        int16_t s = (int16_t)(8000.0 * std::sin(2.0 * 3.14159265358979323846 * hz * i / rate));
        pcm.push_back(s); pcm.push_back(s);
    }
    const uint32_t data = (uint32_t)(pcm.size() * 2);
    std::vector<uint8_t> f;
    auto put32 = [&](uint32_t x) { for (int i = 0; i < 4; i++) f.push_back((x >> (8 * i)) & 0xff); };
    auto put16 = [&](uint16_t x) { f.push_back(x & 0xff); f.push_back(x >> 8); };
    f.insert(f.end(), {'R', 'I', 'F', 'F'}); put32(36 + data);
    f.insert(f.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); put32(16);
    put16(1); put16(2); put32((uint32_t)rate); put32((uint32_t)(rate * 4)); put16(4); put16(16);
    f.insert(f.end(), {'d', 'a', 't', 'a'}); put32(data);
    const uint8_t* b = (const uint8_t*)pcm.data(); f.insert(f.end(), b, b + data);
    FILE* fp = std::fopen(path.c_str(), "wb"); assert(fp);
    std::fwrite(f.data(), 1, f.size(), fp); std::fclose(fp);
}

static double rms_of(const std::string& path) {
    WavFileSource src;
    if (!src.open(path)) return -1.0;
    std::vector<int16_t> buf(4096 * 2);
    double acc = 0; long long n = 0; int r;
    while ((r = src.read(buf.data(), 4096)) > 0)
        for (int i = 0; i < r * 2; i++) { double s = buf[i]; acc += s * s; n++; }
    return n ? std::sqrt(acc / n) : 0.0;
}

int main() {
    std::system("mkdir -p build/test-dsb && rm -f build/test-dsb/*.wav");
    make_wav("build/test-dsb/music.wav", 44100, 1.0, 660.0);

    EngineConfig ec;
    ec.out_rate = 48000;
    ec.wav_path = "build/test-dsb/out.wav";
    ec.music_resolver = [](const std::string&) { return std::string("build/test-dsb/music.wav"); };
    // El sink de música es estable: se puede pedir ANTES de arrancar el Engine.
    CHECK(ds_backend_music() != nullptr, "music sink estable");
    ds_backend_music()->play(1, "MUSIC\\Track1.wav", 0, 0);
    CHECK(ds_backend_start(ec), "start host");
    CHECK(ds_backend_running(), "running");

    // 1 s de efectos 440 Hz @22050 en granos de 512.
    std::vector<int16_t> grain(512 * 2);
    int produced = 0; const int want = 22050;
    while (produced < want) {
        const int n = (want - produced) < 512 ? (want - produced) : 512;
        for (int i = 0; i < n; i++) {
            int16_t s = (int16_t)(7000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * (produced + i) / 22050.0));
            grain[i * 2] = s; grain[i * 2 + 1] = s;
        }
        ds_backend_feed(grain.data(), n, 22050);
        produced += n;
    }
    ds_backend_stop();

    WavFileSource rd;
    CHECK(rd.open(ec.wav_path), "wav abierto");
    CHECK(rd.rate() == 48000, "wav 48000");
    CHECK(rd.total_frames() > 0, "wav no vacío");
    CHECK(rms_of(ec.wav_path) > 1000, "wav con señal");
    std::printf("info ds_bridge: frames=%llu rms=%.1f\n",
                (unsigned long long)rd.total_frames(), rms_of(ec.wav_path));

    // En host el sink Vita no existe: start(vita) debe fallar (no fingir).
    EngineConfig v; v.vita = true; v.wav_path = "build/test-dsb/void.wav";
    CHECK(!ds_backend_start(v), "vita no arranca en host");

    if (g_fail) { std::printf("DS_BRIDGE: %d FALLAS\n", g_fail); return 1; }
    std::printf("DS_BRIDGE: PASS\n");
    return 0;
}
