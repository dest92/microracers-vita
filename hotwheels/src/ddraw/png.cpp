#include "png.h"
#include "host/frame_retention.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace hw::ddraw {
namespace {
// Perf (medido 2026-10-03, perfilador PC host en qemu): este codificador era
// ~54 % del CPU de una carrera qemu (CRC bit a bit, Adler con % por byte,
// push_back por byte), y FrameRetention guarda TODOS los frames (cola de 20).
// Misma salida byte a byte; sólo cambia el costo.
// CRC32 slice-by-8 (8 tablas): 8 bytes por iteración en vez de 1.
struct CrcTable {
    uint32_t t[8][256];
    CrcTable() {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (unsigned j = 0; j < 8; ++j) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
            t[0][n] = c;
        }
        for (uint32_t n = 0; n < 256; ++n)
            for (unsigned k = 1; k < 8; ++k) t[k][n] = (t[k - 1][n] >> 8) ^ t[0][t[k - 1][n] & 0xffu];
    }
};
const CrcTable& crc_table() { static const CrcTable tab; return tab; }
uint32_t crc_update(uint32_t c, const uint8_t* p, size_t n) {
    const auto& t = crc_table().t;
    while (n >= 8) {
        const uint32_t a = c ^ (uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
        const uint32_t b = uint32_t(p[4]) | uint32_t(p[5]) << 8 | uint32_t(p[6]) << 16 | uint32_t(p[7]) << 24;
        c = t[7][a & 0xff] ^ t[6][(a >> 8) & 0xff] ^ t[5][(a >> 16) & 0xff] ^ t[4][a >> 24] ^
            t[3][b & 0xff] ^ t[2][(b >> 8) & 0xff] ^ t[1][(b >> 16) & 0xff] ^ t[0][b >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = t[0][(c ^ *p++) & 0xffu] ^ (c >> 8);
    return c;
}
inline void put_be32(uint8_t* p, uint32_t n) {
    p[0] = uint8_t(n >> 24); p[1] = uint8_t(n >> 16); p[2] = uint8_t(n >> 8); p[3] = uint8_t(n);
}
void be32(std::vector<uint8_t>& v, uint32_t n) { uint8_t b[4]; put_be32(b, n); v.insert(v.end(), b, b + 4); }
void chunk(std::vector<uint8_t>& png, const char type[4], const uint8_t* data, size_t n) {
    be32(png, uint32_t(n));
    uint32_t c = crc_update(~0u, reinterpret_cast<const uint8_t*>(type), 4);
    c = crc_update(c, data, n);
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data, data + n);
    be32(png, ~c);
}
// Adler-32 con módulo diferido (NMAX = 5552: máximo sin desbordar 32 bits).
uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    while (n) {
        size_t k = n < 5552 ? n : 5552; n -= k;
        while (k--) { a += *p++; b += a; }
        a %= 65521; b %= 65521;
    }
    return (b << 16) | a;
}
}
void PngPresenter::present(const uint8_t* idx,uint32_t pitch,const uint32_t pal[256]) {
    if(!idx||!width_||!height_||pitch<width_) throw std::runtime_error("PNG input invalido");
    const size_t row = 1 + 4 * size_t(width_), rawn = size_t(height_) * row;
    // zlib/DEFLATE con bloques sin compresión: portable para host y tests.
    // Se escribe el payload crudo directamente dentro de los bloques stored.
    const size_t nblocks = (rawn + 65534) / 65535;
    // Sin inicializar a cero: todo byte se escribe abajo (el memset era ~3 %).
    std::unique_ptr<uint8_t[]> zbuf(new uint8_t[2 + nblocks * 5 + rawn + 4]);
    uint8_t* const z = zbuf.get();
    z[0] = 0x78; z[1] = 0x01;
    std::unique_ptr<uint8_t[]> rawbuf(new uint8_t[rawn]);
    uint8_t* const raw = rawbuf.get();
    for (uint32_t y = 0; y < height_; ++y) {
        uint8_t* o = raw + size_t(y) * row;
        *o++ = 0;
        const uint8_t* in = idx + size_t(y) * pitch;
        for (uint32_t x = 0; x < width_; ++x) {
            const uint32_t c = pal[in[x]];
            o[0] = uint8_t(c >> 16); o[1] = uint8_t(c >> 8); o[2] = uint8_t(c); o[3] = uint8_t(c >> 24);
            o += 4;
        }
    }
    size_t zo = 2;
    for (size_t at = 0; at < rawn;) {
        const size_t n = std::min<size_t>(65535, rawn - at); const bool last = at + n == rawn;
        z[zo++] = last ? 1 : 0; z[zo++] = uint8_t(n); z[zo++] = uint8_t(n >> 8);
        z[zo++] = uint8_t(~n); z[zo++] = uint8_t((~n) >> 8);
        std::memcpy(z + zo, raw + at, n); zo += n; at += n;
    }
    put_be32(z + zo, adler32(raw, rawn)); zo += 4;
    std::vector<uint8_t> png{137,80,78,71,13,10,26,10};
    png.reserve(8 + 25 + 12 + zo + 12);
    uint8_t ihdr[13]; put_be32(ihdr, width_); put_be32(ihdr + 4, height_);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(png, "IHDR", ihdr, sizeof ihdr); chunk(png, "IDAT", z, zo); chunk(png, "IEND", nullptr, 0);
    const bool saved = hw::host::FrameRetention::save(path_, [&](const std::string& target) {
        std::ofstream f(target, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        return bool(f);
    });
    if (!saved) throw std::runtime_error("PNG write fallo");
}
}
