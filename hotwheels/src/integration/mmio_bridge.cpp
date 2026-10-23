// src/integration/mmio_bridge.cpp — ver mmio_bridge.h.
// Layouts Win32 (PE32): MMIOINFO = 72 bytes (cchBuffer@20 pchBuffer@24 pchNext@28
// pchEndRead@32 lBufOffset@40 lDiskOffset@44 hmmio@68); MMCKINFO = 20 bytes.
#include "integration/mmio_bridge.h"
#include "integration/mmio_transfer.h"

#include "shims/shims.h"
#include "winmm/mmio.h"

#include "runtime/bridge.h"
#include "runtime/cpu.h"

#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hw::integration {
namespace {
using namespace hw::winmm;

struct VfsSource : FileSource {
    hw::shims::Vfs& vfs;
    explicit VfsSource(hw::shims::Vfs& v) : vfs(v) {}
    bool read(const std::string& win, std::vector<uint8_t>& out) override {
        const std::string host = vfs.resolve_read(win);
        if (host.empty()) return false;
        std::ifstream f(host, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
};

struct GBuf { uint32_t va = 0, cap = 0; };
struct State {
    std::unique_ptr<VfsSource> src;
    std::unique_ptr<Mmio> mmio;
    std::map<HMMIO, GBuf> bufs;
    uint32_t (*alloc)(uint32_t) = nullptr;
};
State g;

std::string guest_cstr(d2rt::Cpu& c, uint32_t va) {
    std::string s;
    for (uint8_t b; va && s.size() < 260 && (c.read(va + (uint32_t)s.size(), &b, 1), b) != 0;) s += char(b);
    return s;
}
MmckInfo rd_ck(d2rt::Cpu& c, uint32_t p) {
    MmckInfo k; if (!p) return k;
    k.ckid = c.read_u32(p); k.cksize = c.read_u32(p + 4); k.fccType = c.read_u32(p + 8);
    k.dwDataOffset = c.read_u32(p + 12); k.dwFlags = c.read_u32(p + 16);
    return k;
}
void wr_ck(d2rt::Cpu& c, uint32_t p, const MmckInfo& k) {
    if (!p) return;
    c.write_u32(p, k.ckid); c.write_u32(p + 4, k.cksize); c.write_u32(p + 8, k.fccType);
    c.write_u32(p + 12, k.dwDataOffset); c.write_u32(p + 16, k.dwFlags);
}
// host MmioInfo (punteros a File::buf) -> guest MMIOINFO (punteros a GBuf).
void wr_info(d2rt::Cpu& c, uint32_t p, HMMIO h, const MmioInfo& i) {
    if (!p) return;
    uint32_t base = 0;
    if (i.pchBuffer && i.cchBuffer > 0) {
        GBuf& b = g.bufs[h];
        if (b.cap < (uint32_t)i.cchBuffer) { b.va = g.alloc((uint32_t)i.cchBuffer); b.cap = b.va ? (uint32_t)i.cchBuffer : 0; }
        base = b.va;
        if (base) c.write(base, i.pchBuffer, (uint32_t)i.cchBuffer);
    }
    auto tr = [&](const uint8_t* q) { return (base && q) ? base + (uint32_t)(q - i.pchBuffer) : 0u; };
    c.write_u32(p + 20, (uint32_t)i.cchBuffer);
    c.write_u32(p + 24, base);
    c.write_u32(p + 28, tr(i.pchNext));
    c.write_u32(p + 32, tr(i.pchEndRead));
    c.write_u32(p + 36, tr(i.pchEndRead));
    c.write_u32(p + 40, (uint32_t)i.lBufOffset);
    c.write_u32(p + 44, (uint32_t)i.lDiskOffset);
    c.write_u32(p + 68, h);
}
// guest MMIOINFO -> host MmioInfo, re-basando los punteros sobre `host_base`.
MmioInfo rd_info(d2rt::Cpu& c, uint32_t p, uint8_t* host_base) {
    MmioInfo i; if (!p) return i;
    i.dwFlags = c.read_u32(p);
    i.cchBuffer = (int32_t)c.read_u32(p + 20);
    const uint32_t gb = c.read_u32(p + 24);
    auto tr = [&](uint32_t q) -> uint8_t* { return (gb && host_base && q >= gb) ? host_base + (q - gb) : nullptr; };
    i.pchBuffer = (gb && host_base) ? host_base : nullptr;
    i.pchNext = tr(c.read_u32(p + 28));
    i.pchEndRead = tr(c.read_u32(p + 32));
    i.lBufOffset = (int32_t)c.read_u32(p + 40);
    i.lDiskOffset = (int32_t)c.read_u32(p + 44);
    return i;
}
// Base host del buffer actual del handle (File::buf), vía get_info.
uint8_t* host_base(HMMIO h) { MmioInfo t; return g.mmio->get_info(h, t) ? nullptr : t.pchBuffer; }

void reg(d2rt::Bridge& br, const char* name, uint32_t argc, uint32_t (*fn)(d2rt::Cpu&)) {
    d2rt::Shim s; s.tag = std::string("WINMM.dll!") + name; s.argc = argc; s.fn = fn;
    br.register_shim("WINMM.dll", name, s);
}
}  // namespace

void install_mmio(d2rt::Bridge& br, hw::shims::Vfs& vfs, uint32_t (*guest_alloc)(uint32_t)) {
    g.src = std::make_unique<VfsSource>(vfs);
    g.mmio = std::make_unique<Mmio>(*g.src);
    g.alloc = guest_alloc;
    reg(br, "mmioOpenA", 3, [](d2rt::Cpu& c) -> uint32_t {
        const std::string name = guest_cstr(c, c.arg(0));
        MmioInfo i;
        const HMMIO h = g.mmio->open(name.c_str(), &i, c.arg(2));
        if (c.arg(1)) { c.write_u32(c.arg(1) + 12, h ? 0u : i.wErrorRet); if (h) c.write_u32(c.arg(1) + 68, h); }
        return h;
    });
    reg(br, "mmioClose", 2, [](d2rt::Cpu& c) -> uint32_t { g.bufs.erase(c.arg(0)); return g.mmio->close(c.arg(0)); });
    reg(br, "mmioRead", 3, [](d2rt::Cpu& c) -> uint32_t {
        const int32_t n = (int32_t)c.arg(2);
        const uint32_t destination = c.arg(1);
        return uint32_t(detail::mmio_read_bounded(n,
            [&](uint8_t* buffer, int32_t request) {
                return g.mmio->read(c.arg(0), buffer, request);
            },
            [&](const uint8_t* buffer, uint32_t bytes, uint32_t offset) {
                if (offset > UINT32_MAX - destination) return false;
                return c.write(destination + offset, buffer, bytes);
            }));
    });
    reg(br, "mmioSeek", 3, [](d2rt::Cpu& c) -> uint32_t {
        return (uint32_t)g.mmio->seek(c.arg(0), (int32_t)c.arg(1), (int)c.arg(2)); });
    reg(br, "mmioDescend", 4, [](d2rt::Cpu& c) -> uint32_t {
        MmckInfo ck = rd_ck(c, c.arg(1)), parent = rd_ck(c, c.arg(2));
        const uint32_t e = g.mmio->descend(c.arg(0), ck, c.arg(2) ? &parent : nullptr, c.arg(3));
        if (!e) wr_ck(c, c.arg(1), ck);
        return e;
    });
    reg(br, "mmioAscend", 3, [](d2rt::Cpu& c) -> uint32_t {
        MmckInfo ck = rd_ck(c, c.arg(1));
        const uint32_t e = g.mmio->ascend(c.arg(0), ck, c.arg(2));
        wr_ck(c, c.arg(1), ck);
        return e;
    });
    reg(br, "mmioGetInfo", 3, [](d2rt::Cpu& c) -> uint32_t {
        MmioInfo i; const uint32_t e = g.mmio->get_info(c.arg(0), i);
        if (!e) wr_info(c, c.arg(1), c.arg(0), i);
        return e;
    });
    reg(br, "mmioSetInfo", 3, [](d2rt::Cpu& c) -> uint32_t {
        return g.mmio->set_info(c.arg(0), rd_info(c, c.arg(1), host_base(c.arg(0)))); });
    reg(br, "mmioAdvance", 3, [](d2rt::Cpu& c) -> uint32_t {
        // Win32: la posición avanza hasta pchNext antes de recargar el buffer.
        MmioInfo i = rd_info(c, c.arg(1), host_base(c.arg(0)));
        if (i.pchBuffer) g.mmio->set_info(c.arg(0), i);
        const uint32_t e = g.mmio->advance(c.arg(0), i, c.arg(2));
        if (!e) wr_info(c, c.arg(1), c.arg(0), i);
        return e;
    });
}

}  // namespace hw::integration
