// tests/test_read_stream.cpp — HW-PERF: ReadStream (readahead + seek sin syscall).
#include "io/read_stream.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, m); ++g_fail; } } while (0)
using hw::io::ReadStream;
int main() {
    std::string p = "build/rs_test.bin";
    std::vector<uint8_t> data(200000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 7 + i / 251);
    { FILE* f = std::fopen(p.c_str(), "wb"); std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
    FILE* f = std::fopen(p.c_str(), "rb");
    ReadStream rs(f, 4096);
    CHECK(rs.size() == data.size(), "size");
    // lecturas chicas secuenciales: 1 solo refill por ventana
    uint8_t b[512];
    for (int i = 0; i < 8; ++i) CHECK(rs.read(b, 512) == 512 && !std::memcmp(b, &data[i * 512], 512), "seq");
    CHECK(rs.syscalls() == 1, "8x512B = 1 refill");
    // seek que no mueve: cero syscalls
    uint64_t s0 = rs.syscalls();
    CHECK(rs.seek(0, 1) == 4096, "seek cur 0");
    CHECK(rs.seek(0, 2) == (int64_t)data.size(), "seek end");
    CHECK(rs.seek(4096, 0) == 4096, "seek set");
    CHECK(rs.syscalls() == s0, "seek sin syscall");
    // seek dentro de ventana: sin syscall; fuera: refill
    rs.seek(100, 0);
    CHECK(rs.read(b, 16) == 16 && !std::memcmp(b, &data[100], 16), "in-window");
    CHECK(rs.syscalls() == s0, "in-window sin syscall");
    rs.seek(150000, 0);
    CHECK(rs.read(b, 16) == 16 && !std::memcmp(b, &data[150000], 16), "far");
    // lectura grande directa y fin de archivo
    std::vector<uint8_t> big(100000);
    rs.seek(150000, 0);
    CHECK(rs.read(big.data(), big.size()) == 50000 && !std::memcmp(big.data(), &data[150000], 50000), "eof");
    CHECK(rs.read(b, 4) == 0, "eof 0");
    CHECK(rs.seek(-1, 0) == (int64_t)-1, "seek negativo falla");
    CHECK(rs.tell() == data.size(), "pos intacta");
    rs.seek(10, 0);
    CHECK(rs.read(big.data(), 100000) == 100000 && !std::memcmp(big.data(), &data[10], 100000), "big");
    std::fclose(f);
    // BlobCache: segunda apertura = hit, sin releer; invalidate fuerza recarga
    auto& bc = hw::io::BlobCache::instance();
    bc.configure(1u << 20, 16u << 20);
    auto b1 = bc.get(p); auto b2 = bc.get(p);
    CHECK(b1 && b1 == b2 && bc.hits == 1 && bc.misses == 1, "blob hit");
    ReadStream mem(b1);
    mem.seek(777, 0);
    CHECK(mem.read(b, 16) == 16 && !std::memcmp(b, &data[777], 16) && mem.syscalls() == 0, "blob read");
    CHECK(mem.seek(0, 2) == (int64_t)data.size() && mem.read(b, 4) == 0, "blob eof");
    bc.invalidate(p);
    CHECK(bc.get(p) != b1 && bc.misses == 2, "invalidate");
    bc.configure(100, 100);
    bc.invalidate(p);
    CHECK(bc.get(p) == nullptr, "demasiado grande -> bypass");
    if (g_fail) { std::printf("RS: %d FALLAS\n", g_fail); return 1; }
    std::printf("RS: PASS\n");
}
