// tests/test_audio.cpp — HW-018 (spec 009): CA1..CA6 del backend de audio host.
// No usa vitasdk ni winx86: sólo el módulo portable hw::audio.
#include "audio/mixer.h"
#include "audio/wav.h"
#include "audio/music.h"
#include "audio/dsound_bus.h"
#include "audio/engine.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <sys/resource.h>

using namespace hw::audio;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++g_fail; } } while (0)
#define CHECK_NEAR(a, b, tol, msg) do { double _d = std::fabs((double)(a)-(double)(b)); if (_d > (tol)) { std::printf("FAIL %s:%d %s (%.4f vs %.4f)\n", __FILE__, __LINE__, msg, (double)(a), (double)(b)); ++g_fail; } } while (0)

// ---- helpers ---------------------------------------------------------------

// Fuente de un vector de frames estéreo, a una tasa dada.
struct VecSource : PcmSource {
    std::vector<int16_t> data;  // intercalado L,R
    int r;
    size_t pos = 0;
    VecSource(std::vector<int16_t> d, int rate) : data(std::move(d)), r(rate) {}
    int read(int16_t* out, int frames) override {
        int n = 0;
        while (n < frames && (pos + 2) <= data.size()) {
            out[n * 2] = data[pos]; out[n * 2 + 1] = data[pos + 1];
            pos += 2; ++n;
        }
        return n;
    }
    int rate() const override { return r; }
};

static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xff); }

// WAV PCM16 estéreo sintético de `seconds` con un seno de `hz`.
static void make_wav(const std::string& path, int rate, double seconds, double hz, double amp = 8000.0) {
    std::vector<int16_t> pcm;
    const int frames = (int)(rate * seconds);
    for (int i = 0; i < frames; i++) {
        int16_t s = (int16_t)(amp * std::sin(2.0 * 3.14159265358979323846 * hz * i / rate));
        pcm.push_back(s); pcm.push_back(s);
    }
    std::vector<uint8_t> f;
    const uint32_t data = (uint32_t)(pcm.size() * 2);
    f.insert(f.end(), {'R', 'I', 'F', 'F'}); put32(f, 36 + data);
    f.insert(f.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); put32(f, 16);
    put16(f, 1); put16(f, 2); put32(f, (uint32_t)rate); put32(f, (uint32_t)(rate * 4)); put16(f, 4); put16(f, 16);
    f.insert(f.end(), {'d', 'a', 't', 'a'}); put32(f, data);
    const uint8_t* b = (const uint8_t*)pcm.data();
    f.insert(f.end(), b, b + data);
    FILE* fp = std::fopen(path.c_str(), "wb");
    assert(fp);
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);
}

static std::vector<int16_t> make_ramp_wav(const std::string& path, int frames) {
    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        pcm.push_back(static_cast<int16_t>((i * 31) % 30001 - 15000));
        pcm.push_back(static_cast<int16_t>((i * 17) % 28001 - 14000));
    }
    std::vector<uint8_t> f;
    const uint32_t data = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    f.insert(f.end(), {'R','I','F','F'}); put32(f, 36 + data);
    f.insert(f.end(), {'W','A','V','E','f','m','t',' '}); put32(f, 16);
    put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 176400); put16(f, 4); put16(f, 16);
    f.insert(f.end(), {'d','a','t','a'}); put32(f, data);
    const auto* bytes = reinterpret_cast<const uint8_t*>(pcm.data());
    f.insert(f.end(), bytes, bytes + data);
    FILE* out = std::fopen(path.c_str(), "wb"); assert(out);
    assert(std::fwrite(f.data(), 1, f.size(), out) == f.size());
    std::fclose(out);
    return pcm;
}

// RMS de un WAV (lee con WavFileSource, la clase bajo prueba).
static double rms_of(const std::string& path) {
    WavFileSource src;
    if (!src.open(path)) return -1.0;
    std::vector<int16_t> buf(4096 * 2);
    double acc = 0; long long n = 0; int r;
    while ((r = src.read(buf.data(), 4096)) > 0) {
        for (int i = 0; i < r * 2; i++) { double s = buf[i]; acc += s * s; n++; }
    }
    return n ? std::sqrt(acc / n) : 0.0;
}

// ---- CA1: mezcla y saturación ----------------------------------------------
static void ca1_mixer() {
    Mixer mx;
    VecSource a({30000, 30000, -30000, -30000}, 48000);
    mx.add(&a, 1.0f);
    int16_t out[2] = {0, 0};
    mx.mix(out, 1);
    CHECK(out[0] == 30000 && out[1] == 30000, "CA1 fuente única");

    Mixer mx2;
    VecSource b({20000, -20000, 0, 0}, 48000);
    VecSource c({20000, -20000, 0, 0}, 48000);
    mx2.add(&b, 1.0f); mx2.add(&c, 1.0f);
    int16_t o2[4] = {0};
    mx2.mix(o2, 2);
    CHECK(o2[0] == 32767 && o2[1] == -32768, "CA1 saturación");
    CHECK(o2[2] == 0 && o2[3] == 0, "CA1 suma sin clip");

    // Fuente vacía no rompe a la otra.
    Mixer mx3;
    VecSource empty({}, 48000);
    VecSource tone({1000, 1000, 1000, 1000}, 48000);
    mx3.add(&empty); mx3.add(&tone);
    int16_t o3[2] = {0};
    mx3.mix(o3, 1);
    CHECK(o3[0] == 1000, "CA1 fuente vacía no altera");
}

// ---- CA2: resampleo 22050 -> 48000 -----------------------------------------
static void ca2_resample() {
    std::vector<int16_t> ramp;
    for (int i = 0; i < 22200; i++) { ramp.push_back((int16_t)i); ramp.push_back((int16_t)i); }
    VecSource s(ramp, 22050);
    Mixer mx;
    mx.add(&s, 1.0f);
    std::vector<int16_t> out(48000 * 2, 0);
    mx.mix(out.data(), 48000);
    CHECK(out[0] == 0, "CA2 salida[0]");
    // n = 47999 -> p = 47999*22050/48000 = 22049.54 -> in[22049]=22049 .. interp ~22050
    CHECK_NEAR(out[(47999) * 2], 22050, 2, "CA2 salida final");
    // p = 2*0.459375 = 0.91875 -> 0.9 -> 0
    CHECK(out[2 * 2] == 0, "CA2 salida[2]");
    // p en n=100000? no; verificamos monotonicidad básica en la mitad
    CHECK(out[24000 * 2] > out[12000 * 2], "CA2 monótona");
    // frames consumidos ~ 22050 (± bloque)
    const long long consumed = (long long)s.pos / 2;
    CHECK(consumed >= 22050 - 4096 && consumed <= 22050 + 4096, "CA2 consumo ~22050");

    // spec CA2: 22050 frames de entrada -> 48000 de salida (±1). Se cuentan los
    // frames con señal emitidos en bloques de 10 ms, hasta que la fuente se agota
    // (después el mezclador emite silencio exacto).
    std::vector<int16_t> exact;
    exact.reserve(22050 * 2);
    for (int i = 0; i < 22050; i++) { exact.push_back((int16_t)i); exact.push_back((int16_t)i); }
    VecSource s2(std::move(exact), 22050);
    Mixer mx2;
    mx2.add(&s2, 1.0f);
    std::vector<int16_t> blk(480 * 2, 0);
    long long last_signal = -1;
    for (long long n = 0; n <= 60000; n += 480) {
        mx2.mix(blk.data(), 480);
        for (int i = 0; i < 480; i++)
            if (blk[(size_t)i * 2] || blk[(size_t)i * 2 + 1]) last_signal = n + i;
    }
    CHECK_NEAR(last_signal + 1, 48000, 1, "CA2 salida 48000 frames por 22050 de entrada");
}

// ---- CA3: WavWriter --------------------------------------------------------
static void ca3_wavwriter() {
    const std::string path = "build/test-audio-ca3.wav";
    WavWriter w(path);
    CHECK(w.open(48000, 2), "CA3 open");
    std::vector<int16_t> pcm(480 * 2);
    for (int i = 0; i < 480; i++) { pcm[i * 2] = (int16_t)(i * 10); pcm[i * 2 + 1] = (int16_t)(-i * 10); }
    w.write(pcm.data(), 480);
    w.close();

    // Reabrir y validar header + datos con WavFileSource.
    WavFileSource src;
    CHECK(src.open(path), "CA3 reopen");
    CHECK(src.rate() == 48000, "CA3 rate");
    std::vector<int16_t> got(480 * 2);
    CHECK(src.read(got.data(), 480) == 480, "CA3 frames");
    CHECK(got[0] == 0 && got[1] == 0 && got[20] == 100 && got[21] == -100, "CA3 datos");
}

// ---- CA4: MusicPlayer ------------------------------------------------------
static void ca4_music() {
    make_wav("build/test-audio-track.wav", 44100, 0.2, 440.0);
    MusicPlayer mp([](const std::string&) { return std::string("build/test-audio-track.wav"); });
    mp.play(2, "MUSIC\\Track2.wav", 0, 0);
    std::vector<int16_t> buf(1024 * 2);
    int got = mp.read(buf.data(), 1024);
    CHECK(got > 0, "CA4 produce");
    mp.pause();
    CHECK(mp.read(buf.data(), 1024) == 0, "CA4 pause corta");
    mp.stop();

    // Path inexistente => silencio, sin excepción.
    MusicPlayer mp2([](const std::string&) { return std::string("no-existe.wav"); });
    mp2.play(1, "MUSIC\\Track1.wav", 0, 0);
    CHECK(mp2.read(buf.data(), 64) == 0, "CA4 ausente = silencio");
}

// ---- CA5/CA6: Engine end-to-end -------------------------------------------
static void ca5_engine() {
    make_wav("build/test-audio-music.wav", 44100, 1.0, 660.0);  // música 44100
    const char* out_full = "build/test-audio-full.wav";
    const char* out_ds = "build/test-audio-ds.wav";
    const char* out_mu = "build/test-audio-mu.wav";

    auto run = [&](const char* path, bool ds, bool music) {
        Engine e;
        EngineConfig cfg; cfg.wav_path = path; cfg.out_rate = 48000;
        cfg.music_resolver = [](const std::string&) { return std::string("build/test-audio-music.wav"); };
        CHECK(e.start(cfg), "CA5 start");
        if (music) {
            e.music()->play(1, "MUSIC\\Track1.wav", 0, 0);
        }
        // ds: 1 s de 440 Hz @22050
        std::vector<int16_t> dsbuf;
        for (int i = 0; i < 22050; i++) {
            int16_t s = (int16_t)(8000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 22050.0));
            dsbuf.push_back(s); dsbuf.push_back(s);
        }
        if (ds) e.dsound().push(dsbuf.data(), 22050, 22050);
        std::vector<int16_t> tmp(2048 * 2);
        int done = 0;
        while (done < 48000) {
            int n = (48000 - done) < 2048 ? (48000 - done) : 2048;
            // el Mixer tira de las fuentes; el engine mezcla y escribe
            e.pump(n);
            done += n;
            if (music) std::this_thread::sleep_for(std::chrono::milliseconds(43));
        }
        e.stop();
    };
    run(out_full, true, true);
    run(out_ds, true, false);
    run(out_mu, false, true);

    // CA6: el WAV de salida es 48000 siempre.
    { WavFileSource s; CHECK(s.open(out_full) && s.rate() == 48000, "CA6 salida 48000"); }

    const double full = rms_of(out_full), ds = rms_of(out_ds), mu = rms_of(out_mu);
    std::printf("info audio: rms full=%.1f ds=%.1f music=%.1f\n", full, ds, mu);
    CHECK(full > 1000, "CA5 señal");
    CHECK(ds > 1000, "CA5 ds aporta");
    CHECK(mu > 1000, "CA5 música aporta");
    CHECK(full > ds * 1.05 && full > mu * 1.05, "CA5 ambos aportan");
}

static void ca7_no_wav_sink() {
    Engine e;
    EngineConfig cfg; cfg.out_rate = 48000;  // host por defecto: sin evidencia WAV
    CHECK(e.start(cfg), "CA7 sink silencioso sin WAV arranca");
    e.pump(128);
    e.stop();
}

// ---- CA13: chunk fmt sobredimensionado no desborda (regresión wav.cpp) -----
static void ca13_fmt_oversized() {
    const std::string path = "build/test-audio-fmt.wav";
    std::vector<int16_t> pcm(480 * 2, 1234);
    const uint32_t data = (uint32_t)(pcm.size() * 2);
    const uint32_t fmtSize = 48;  // 16 válidos + 32 extra
    std::vector<uint8_t> f;
    f.insert(f.end(), {'R', 'I', 'F', 'F'}); put32(f, 4 + (8 + fmtSize) + (8 + data));
    f.insert(f.end(), {'W', 'A', 'V', 'E'});
    f.insert(f.end(), {'f', 'm', 't', ' '}); put32(f, fmtSize);
    put16(f, 1); put16(f, 2); put32(f, 48000); put32(f, 48000 * 4); put16(f, 4); put16(f, 16);
    for (int i = 0; i < 32; i++) f.push_back(0);
    f.insert(f.end(), {'d', 'a', 't', 'a'}); put32(f, data);
    const uint8_t* b = (const uint8_t*)pcm.data(); f.insert(f.end(), b, b + data);
    FILE* fp = std::fopen(path.c_str(), "wb"); assert(fp);
    std::fwrite(f.data(), 1, f.size(), fp); std::fclose(fp);

    WavFileSource src;
    CHECK(src.open(path), "CA13 abre fmt 48");
    CHECK(src.rate() == 48000, "CA13 rate");
    std::vector<int16_t> got(480 * 2);
    CHECK(src.read(got.data(), 480) == 480, "CA13 lee");
}

// ---- CA14: stream WAV grande sin cargar los datos completos ---------------
static void ca14_large_stream() {
    const std::string path = "build/test-audio-large.wav";
    constexpr uint32_t data_bytes = 50u * 1024u * 1024u;
    FILE* f = std::fopen(path.c_str(), "wb"); assert(f);
    std::vector<uint8_t> h;
    h.insert(h.end(), {'R','I','F','F'}); put32(h, 36u + data_bytes);
    h.insert(h.end(), {'W','A','V','E','f','m','t',' '}); put32(h, 16);
    put16(h, 1); put16(h, 2); put32(h, 44100); put32(h, 176400); put16(h, 4); put16(h, 16);
    h.insert(h.end(), {'d','a','t','a'}); put32(h, data_bytes);
    assert(std::fwrite(h.data(), 1, h.size(), f) == h.size());
    assert(std::fseek(f, 44 + static_cast<long>(data_bytes) - 1, SEEK_SET) == 0);
    std::fputc(0, f); std::fclose(f);
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    WavFileSource src;
    CHECK(src.open(path), "CA14 abre WAV 50 MiB");
    CHECK(src.total_frames() == data_bytes / 4, "CA14 expone frames sin materializar payload");
    int16_t buf[512 * 2]{};
    CHECK(src.read(buf, 512) == 512, "CA14 pull acotado del WAV grande");
    src.close();
    MusicPlayer player([](const std::string&) { return std::string("build/test-audio-large.wav"); });
    player.play(1, "MUSIC\\Track1.wav", 0, 0);
    CHECK(player.read(buf, 512) == 512, "CA14 consumer lee bloque del WAV grande");
    player.stop();
    getrusage(RUSAGE_SELF, &after);
    CHECK(after.ru_maxrss <= before.ru_maxrss + 8 * 1024,
          "CA14 memoria residente adicional <8 MiB para stream de WAV 50 MiB");
}

// ---- CA15: órdenes MCI concurrentes con el pull del mixer ----------------
static void ca15_music_concurrent_control() {
    const std::string path = "build/test-audio-concurrent.wav";
    make_wav(path, 44100, 0.5, 440.0);
    MusicPlayer mp([&](const std::string&) { return path; });
    std::atomic<bool> go{false};
    std::thread mixer([&] {
        int16_t block[256 * 2]{};
        while (!go.load(std::memory_order_acquire)) {}
        for (int i = 0; i < 2000; ++i) (void)mp.read(block, 256);
    });
    go.store(true, std::memory_order_release);
    for (int i = 0; i < 100; ++i) {
        mp.play(1, "MUSIC\\Track1.wav", 0, 0);
        if ((i & 1) == 0) mp.pause(); else mp.stop();
        (void)mp.playing();
        (void)mp.paused();
        (void)mp.track();
        (void)mp.rate();
    }
    mixer.join();
    mp.stop();
}

// ---- CA16: ring MCI preserva muestras y wrap en pulls irregulares ----------
static void ca16_music_ring_samples() {
    constexpr int frames = 20000;
    const std::string path = "build/test-audio-ramp.wav";
    const auto expected = make_ramp_wav(path, frames);
    const int pattern[] = {13, 257, 511, 7, 1023, 89};
    for (bool loop : {false, true}) {
        MusicPlayer player([&](const std::string&) { return path; });
        player.play(1, "MUSIC\\Track1.wav", 0, loop ? 0u : 1u);
        const int total = frames + (loop ? 333 : 0);
        std::vector<int16_t> actual;
        actual.reserve(static_cast<size_t>(total) * 2);
        int turn = 0, idle_ms = 0;
        while (actual.size() < static_cast<size_t>(total) * 2 && idle_ms < 3000) {
            const int ask = std::min(pattern[turn++ % (sizeof(pattern) / sizeof(pattern[0]))],
                                     (total * 2 - static_cast<int>(actual.size())) / 2);
            std::vector<int16_t> block(static_cast<size_t>(ask) * 2);
            const int got = player.read(block.data(), ask);
            if (got == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++idle_ms;
                continue;
            }
            idle_ms = 0;
            actual.insert(actual.end(), block.begin(), block.begin() + static_cast<size_t>(got) * 2);
        }
        CHECK(actual.size() == static_cast<size_t>(total) * 2, "CA16 drains stream through ring wrap");
        for (int i = 0; i < total && actual.size() == static_cast<size_t>(total) * 2; ++i) {
            const size_t source = static_cast<size_t>(i % frames) * 2;
            CHECK(actual[static_cast<size_t>(i) * 2] == expected[source], "CA16 left sample exact");
            CHECK(actual[static_cast<size_t>(i) * 2 + 1] == expected[source + 1], "CA16 right sample exact");
        }
        player.stop();
    }
}

int main() {
    std::system("mkdir -p build");
    ca1_mixer();
    ca2_resample();
    ca3_wavwriter();
    ca4_music();
    ca5_engine();
    ca7_no_wav_sink();
    ca13_fmt_oversized();
    ca14_large_stream();
    ca15_music_concurrent_control();
    ca16_music_ring_samples();
    if (g_fail) { std::printf("AUDIO: %d FALLAS\n", g_fail); return 1; }
    std::printf("AUDIO: PASS (CA1..CA16)\n");
    return 0;
}
