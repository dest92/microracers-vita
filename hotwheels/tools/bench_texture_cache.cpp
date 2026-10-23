#include "d3d/d3d.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace hw::d3d;

struct Sink final : Backend {
    uint64_t uploads = 0;
    void begin_frame(uint32_t, uint32_t) override {}
    void upload_texture(const TextureData&) override { ++uploads; }
    void free_texture(uint32_t) override {}
    void clear(const Rect&, uint32_t) override {}
    void draw(const DrawCall&) override {}
    void present() override {}
};

int main(int argc, char** argv) {
    const bool cached = argc > 1 && argv[1][0] == '1';
    constexpr uint32_t w = 32, h = 32, iterations = 100000;
    std::vector<uint32_t> words(8 + w * h);
    words[0] = 7; words[1] = w; words[2] = h;
    words[3] = uint32_t(TexFormat::ARGB8888); words[4] = w * 4;
    words[5] = 0; words[6] = words[7] = 0;
    for (size_t i = 0; i < w * h; ++i) words[8 + i] = 0xff000000u | uint32_t(i * 2654435761u);
    Ring frame; frame.put(Op::TexUpload, words.data(), uint32_t(words.size()));
    Sink sink;
    Replayer persistent(sink, cached);
    const auto start = std::chrono::steady_clock::now();
    uint32_t completed = 0;
    for (; completed < iterations; ++completed) {
        persistent.play(frame);
    }
    const uint64_t elapsed_ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count());
    std::printf("fps-window mode=texture-cache-%s completed=%u guest_d3d_hz_milli=%llu "
                "backend_uploads=%llu tex_backend_uploads_per_frame=%.6f elapsed_ms=%llu\n",
                cached ? "after" : "before", completed,
                (unsigned long long)(elapsed_ns ? uint64_t(completed) * 1000000000000ull / elapsed_ns : 0),
                (unsigned long long)sink.uploads, double(sink.uploads) / completed,
                (unsigned long long)(elapsed_ns / 1000000ull));
}
