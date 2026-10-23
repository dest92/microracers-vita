#include "diag/image_ring.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kMaxFrames = 1024;
constexpr uint64_t kMaxWords = 8ull * 1024 * 1024;

bool read_u32(std::istream& in, uint32_t& v) {
    unsigned char b[4];
    if (!in.read(reinterpret_cast<char*>(b), sizeof b)) return false;
    v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    return true;
}

int32_t signed_word(uint32_t word) {
    return word <= 0x7fffffffu ? int32_t(word) : -1 - int32_t(0xffffffffu - word);
}

bool valid_rect(const uint32_t* p, bool empty_allowed) {
    const int32_t x = signed_word(p[0]);
    const int32_t y = signed_word(p[1]);
    return x >= -2048 && x <= 4096 && y >= -2048 && y <= 4096 &&
           p[2] <= 2048 && p[3] <= 2048 &&
           (empty_allowed || (p[2] && p[3]));
}

bool valid_command(uint16_t raw, const uint32_t* p, uint32_t n) {
    using hw::d3d::Op;
    switch (Op(raw)) {
    case Op::Begin: return n == 2 && p[0] > 0 && p[0] <= 2048 && p[1] > 0 && p[1] <= 2048;
    case Op::Viewport: return n == 4 && valid_rect(p, false);
    case Op::Clear: return n == 5 && valid_rect(p, true);
    case Op::State: return n == 2;
    case Op::Tss: return n == 3;
    case Op::Texture: return n == 2;
    case Op::Draw: return n >= 2 && uint64_t(n) == 2ull + uint64_t(p[1]) * 6;
    case Op::End:
    case Op::Present: return n == 0;
    case Op::TexUpload: return n >= 8;
    case Op::TexFree: return n == 1;
    }
    return false;
}

bool load(const std::string& path, std::vector<hw::d3d::Ring>& frames, uint32_t& target) {
    std::ifstream in(path, std::ios::binary);
    char magic[8];
    uint32_t count = 0;
    if (!in.read(magic, sizeof magic) || std::string(magic, sizeof magic) != "HWRING01" ||
        !read_u32(in, count) || !read_u32(in, target) || count == 0 || count > kMaxFrames || target >= count) {
        return false;
    }
    uint64_t total = 0;
    frames.reserve(count);
    for (uint32_t f = 0; f < count; ++f) {
        uint32_t n = 0;
        if (!read_u32(in, n) || n > kMaxWords || n > kMaxWords - total) return false;
        std::vector<uint32_t> words(n);
        if (!in.read(reinterpret_cast<char*>(words.data()), std::streamsize(uint64_t(n) * 4))) return false;
        total += n;
        hw::d3d::Ring ring;
        size_t cursor = 0;
        while (cursor < words.size()) {
            uint16_t op = 0;
            const uint32_t* payload = nullptr;
            uint32_t payload_words = 0;
            if (!hw::d3d::Ring::next(words, cursor, op, payload, payload_words) ||
                !valid_command(op, payload, payload_words)) return false;
            ring.put(static_cast<hw::d3d::Op>(op), payload, payload_words);
        }
        frames.push_back(std::move(ring));
    }
    return in.peek() == std::char_traits<char>::eof();
}

bool replay(const std::vector<hw::d3d::Ring>& frames, hw::d3d::TextureFilter filter,
            const std::string& path) {
    hw::d3d::RefRaster raster;
    hw::d3d::Replayer player(raster, false);
    hw::diag::ImageRingFilter override(filter);
    for (const auto& source : frames) {
        hw::d3d::Ring filtered;
        if (!override.rewrite(source, filtered)) return false;
        player.play(filtered);
    }
    if (!raster.width() || !raster.height() || !raster.write_png(path)) return false;
    std::cout << path << " " << raster.width() << "x" << raster.height()
              << " hash=0x" << std::hex << raster.hash() << std::dec << "\n";
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "uso: image_ring_replay <captura.hwr> <prefijo-salida>\n";
        return 2;
    }
    std::vector<hw::d3d::Ring> frames;
    uint32_t target = 0;
    if (!load(argv[1], frames, target)) {
        std::cerr << "captura HWRING01 inválida\n";
        return 1;
    }
    std::cout << "frames=" << frames.size() << " target_index=" << target << "\n";
    const std::string prefix = argv[2];
    bool ok = true;
    ok &= replay(frames, hw::d3d::TextureFilter::Point, prefix + "-point.png");
    ok &= replay(frames, hw::d3d::TextureFilter::Linear, prefix + "-linear.png");
    ok &= replay(frames, hw::d3d::TextureFilter::Game, prefix + "-game.png");
    return ok ? 0 : 1;
}
