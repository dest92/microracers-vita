// pe_file.cpp — lee un PE32 i386 sin su overlay (el instalador de HW trae
// ~222 MB pegados después de .reloc; ningún byte de overlay debe pasar por la
// RAM de la Vita). Lectura por etapas de prefijos del archivo: cada etapa sólo
// lee lo que ya se sabe que pertenece a cabeceras o secciones.
#include "boot/hw_boot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hw {

namespace {
uint16_t u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
}

bool read_pe_file(const std::string& path, std::vector<uint8_t>& out,
                  PeFileInfo& info, std::string& err) {
    info = PeFileInfo{};
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "no se puede abrir " + path; return false; }
    auto fail = [&](const std::string& m) { std::fclose(f); out.clear(); err = m; return false; };

    if (std::fseek(f, 0, SEEK_END) != 0) return fail("fseek");
    long sz = std::ftell(f);
    if (sz < 0) return fail("ftell");
    info.file_size = (uint64_t)sz;
    std::rewind(f);

    // Extiende el prefijo leído hasta `n` bytes (lectura secuencial; cuenta
    // los bytes físicamente leídos en info.bytes_read).
    auto ensure = [&](uint64_t n) {
        if (n > info.file_size) return false;
        if (n <= out.size()) return true;
        size_t have = out.size();
        out.resize((size_t)n);
        size_t got = std::fread(out.data() + have, 1, (size_t)n - have, f);
        info.bytes_read += got;
        return got == (size_t)n - have;
    };

    if (!ensure(0x40) || out[0] != 'M' || out[1] != 'Z') return fail("no es MZ");
    const uint64_t pe = u32(&out[0x3c]);
    if (!ensure(pe + 24) || std::memcmp(&out[pe], "PE\0\0", 4) != 0) return fail("sin firma PE");
    if (u16(&out[pe + 4]) != 0x14c) return fail("máquina no i386");
    const uint16_t nsec = u16(&out[pe + 6]), optsz = u16(&out[pe + 20]);
    const uint64_t opt = pe + 24, sec = opt + optsz;
    // SizeOfHeaders vive en opt+60: exigir Optional Header de al menos 64 bytes.
    if (optsz < 64) return fail("Optional Header truncado");
    if (!ensure(sec + (uint64_t)nsec * 40)) return fail("cabeceras más allá del fin de archivo");
    if (u16(&out[opt]) != 0x10b) return fail("no es PE32");

    uint64_t end = std::max<uint64_t>(u32(&out[opt + 60]), out.size());   // SizeOfHeaders
    for (uint16_t i = 0; i < nsec; ++i) {
        const uint8_t* s = &out[sec + i * 40];
        uint32_t rsize = u32(s + 16), rptr = u32(s + 20);
        if (rsize) end = std::max<uint64_t>(end, (uint64_t)rptr + rsize);
    }
    if (!ensure(end)) return fail("secciones más allá del fin de archivo");
    std::fclose(f);
    info.overlay_size = info.file_size - end;
    return true;
}

} // namespace hw
