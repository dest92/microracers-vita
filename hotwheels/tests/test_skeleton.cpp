// test_skeleton.cpp — criterios A1–A4 de specs/001-skeleton.md (host, sin dynarec).
#include "boot/hw_boot.h"
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include "fake_cpu.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace d2rt;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// PE32 i386 mínimo: .text (rva 0x1000) + .idata (rva 0x2000) con
// KERNEL32.dll!ExitProcess, y un overlay de `overlay` bytes al final.
static const uint32_t kIatRva = 0x2050, kEntryRva = 0x1000, kBase = 0x400000;
static std::vector<uint8_t> make_pe(uint16_t machine, size_t overlay) {
    std::vector<uint8_t> f(0x600, 0);
    auto w16 = [&](size_t o, uint16_t v) { std::memcpy(&f[o], &v, 2); };
    auto w32 = [&](size_t o, uint32_t v) { std::memcpy(&f[o], &v, 4); };
    f[0] = 'M'; f[1] = 'Z'; w32(0x3c, 0x40);
    std::memcpy(&f[0x40], "PE\0\0", 4);
    const size_t coff = 0x44, opt = coff + 20;
    w16(coff + 0, machine); w16(coff + 2, 2); w16(coff + 16, 224); w16(coff + 18, 0x0102);
    w16(opt + 0, 0x10b); w32(opt + 16, kEntryRva); w32(opt + 28, kBase);
    w32(opt + 32, 0x1000); w32(opt + 36, 0x200);
    w16(opt + 40, 4);                       // major OS version
    w16(opt + 48, 4);                       // major subsystem version
    w32(opt + 56, 0x3000); w32(opt + 60, 0x200); w16(opt + 68, 2);
    w32(opt + 72, 0x100000); w32(opt + 76, 0x1000); w32(opt + 80, 0x100000); w32(opt + 84, 0x1000);
    w32(opt + 92, 16);
    w32(opt + 96 + 8, 0x2000); w32(opt + 96 + 12, 0x80);   // import directory
    const size_t sec = opt + 224;
    std::memcpy(&f[sec], ".text", 5);
    w32(sec + 8, 0x200); w32(sec + 12, 0x1000); w32(sec + 16, 0x200); w32(sec + 20, 0x200);
    w32(sec + 36, 0x60000020);
    std::memcpy(&f[sec + 40], ".idata", 6);
    w32(sec + 48, 0x200); w32(sec + 52, 0x2000); w32(sec + 56, 0x200); w32(sec + 60, 0x400);
    w32(sec + 76, 0xc0000040);
    // .text: call dword [IAT] ; ret
    f[0x200] = 0xff; f[0x201] = 0x15; w32(0x202, kBase + kIatRva); f[0x206] = 0xc3;
    // .idata (raw 0x400 <-> rva 0x2000)
    auto R = [](uint32_t rva) { return (size_t)(rva - 0x2000 + 0x400); };
    w32(R(0x2000) + 0, 0x2040); w32(R(0x2000) + 12, 0x2060); w32(R(0x2000) + 16, kIatRva);
    w32(R(0x2040), 0x2070);
    w32(R(kIatRva), 0x2070);
    std::memcpy(&f[R(0x2060)], "KERNEL32.dll", 12);
    std::memcpy(&f[R(0x2072)], "ExitProcess", 11);
    f.resize(f.size() + overlay, 0xAB);
    return f;
}

static std::string tmpfile_with(const std::vector<uint8_t>& b, const char* name) {
    std::string p = std::string("/tmp/hw_skel_") + name;
    std::ofstream(p, std::ios::binary).write((const char*)b.data(), (std::streamsize)b.size());
    return p;
}

static void test_read_synthetic() {   // A1
    auto pe = make_pe(0x14c, 1 << 20);
    std::string p = tmpfile_with(pe, "ok.exe");
    std::vector<uint8_t> out; hw::PeFileInfo info; std::string err;
    CHECK(hw::read_pe_file(p, out, info, err));
    CHECK(info.file_size == pe.size());
    CHECK(info.bytes_read == 0x600);   // bytes leídos FÍSICAMENTE: ni uno del overlay
    CHECK(info.overlay_size == (1u << 20));
    CHECK(out.size() == 0x600);
    CHECK(out.size() >= 0x600 && std::memcmp(out.data(), pe.data(), 0x600) == 0);
}

static void test_rejects() {   // A2
    std::vector<uint8_t> out; hw::PeFileInfo info; std::string err;
    CHECK(!hw::read_pe_file("/tmp/hw_skel_no_existe.exe", out, info, err) && !err.empty());
    err.clear();
    std::vector<uint8_t> junk(0x400, 0x11);
    CHECK(!hw::read_pe_file(tmpfile_with(junk, "junk.exe"), out, info, err) && !err.empty());
    err.clear();
    CHECK(!hw::read_pe_file(tmpfile_with(make_pe(0x8664, 0), "amd64.exe"), out, info, err) && !err.empty());
    err.clear();
    auto nope = make_pe(0x14c, 0); std::memcpy(&nope[0x40], "XX\0\0", 4);   // MZ sin "PE\0\0"
    CHECK(!hw::read_pe_file(tmpfile_with(nope, "nope.exe"), out, info, err) && !err.empty());
    err.clear();
    auto plus = make_pe(0x14c, 0); plus[0x58] = 0x0b; plus[0x59] = 0x02;      // magic 0x20b (PE32+)
    CHECK(!hw::read_pe_file(tmpfile_with(plus, "pe32plus.exe"), out, info, err) && !err.empty());
    err.clear();
    // Optional Header truncado (review e5c4463): file=0x100, e_lfanew=0xD0,
    // SizeOfOptionalHeader=0, 0 secciones. SizeOfHeaders caería en 0x124 > 0x100.
    std::vector<uint8_t> trunc(0x100, 0);
    trunc[0] = 'M'; trunc[1] = 'Z'; trunc[0x3c] = 0xD0;
    std::memcpy(&trunc[0xD0], "PE\0\0", 4); trunc[0xD4] = 0x4c; trunc[0xD5] = 0x01;
    trunc[0xE8] = 0x0b; trunc[0xE9] = 0x01;
    CHECK(!hw::read_pe_file(tmpfile_with(trunc, "trunc.exe"), out, info, err) && !err.empty());
}

static void test_boot_synthetic() {   // A3
    std::vector<uint8_t> pe; hw::PeFileInfo info; std::string err;
    CHECK(hw::read_pe_file(tmpfile_with(make_pe(0x14c, 4096), "boot.exe"), pe, info, err));
    FakeCpu cpu; Bridge br(&cpu);
    hw::BootOptions opt; hw::BootResult r;
    bool hook_called = false;
    opt.install_shims = [&](Bridge& b, Cpu& c, std::string&) {
        hook_called = true;
        CHECK(c.mapped(b.stack_top() - 1)); // commit ya mapeó la pila
        Shim s; s.argc = 1; s.fn = [](Cpu&) { return 0u; };
        b.register_shim("KERNEL32.dll", "ExitProcess", s);
        return true;
    };
    bool ok = hw::boot_to_entry(br, cpu, pe, opt, r, err);
    if (!ok) std::printf("  boot_to_entry: %s\n", err.c_str());
    CHECK(ok);
    CHECK(hook_called);
    if (!ok) return;
    CHECK(r.load_base == kBase);
    CHECK(r.entry_va == kBase + kEntryRva);
    CHECK(cpu.reg(R_EIP) == r.entry_va);
    CHECK(cpu.mapped(cpu.reg(R_ESP)));
    CHECK(cpu.reg(R_ESP) == br.stack_top() - 0x10);
    CHECK(cpu.read_u32(cpu.reg(R_ESP)) == r.sentinel && r.sentinel != 0);
    CHECK(r.import_count == 1);
    CHECK(r.unresolved_count == 0); // el hook corrió antes de link()
    uint32_t slot = cpu.read_u32(kBase + kIatRva);
    CHECK(slot != 0 && slot != 0x2070);
    CHECK(slot >= cpu.trap_lo && slot < cpu.trap_hi);
    uint8_t code[2] = {};
    CHECK(cpu.read(r.entry_va, code, 2) && code[0] == 0xff && code[1] == 0x15);
}

// A4 apunta al juego real (HotWheels.exe, año 2000, D3D6/DDraw) extraído del
// ISO por HW-001 a hotwheels/extracted/; NO al instalador de 224 MB.
static bool find_game_exe(const std::string& dir, std::string& path) {
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string full = dir + "/" + n, low = n;
        for (auto& c : low) c = (char)tolower((unsigned char)c);
        if (low == "hotwheels.exe") { path = full; break; }
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && find_game_exe(full, path)) break;
    }
    closedir(d);
    return !path.empty();
}

static bool test_real_exe() {   // A4; false = SKIP
    std::string path;
    if (!find_game_exe(HW_EXTRACTED_DIR, path)) {
        std::printf("SKIP A4: no hay HotWheels.exe bajo %s\n", HW_EXTRACTED_DIR); return false;
    }
    std::vector<uint8_t> pe; hw::PeFileInfo info; std::string err;
    bool rd = hw::read_pe_file(path, pe, info, err);
    CHECK(rd);
    if (!rd) { std::printf("  read_pe_file: %s\n", err.c_str()); return true; }
    std::printf("  %s: file=%llu leídos=%llu overlay=%llu\n", path.c_str(),
                (unsigned long long)info.file_size, (unsigned long long)info.bytes_read,
                (unsigned long long)info.overlay_size);
    CHECK(info.bytes_read + info.overlay_size == info.file_size);
    uint32_t pe_off = 0, entry_rva = 0;
    std::memcpy(&pe_off, &pe[0x3c], 4);
    std::memcpy(&entry_rva, &pe[pe_off + 24 + 16], 4);
    FakeCpu cpu; Bridge br(&cpu);
    hw::BootOptions opt; opt.log_unresolved = false; hw::BootResult r;
    bool ok = hw::boot_to_entry(br, cpu, pe, opt, r, err);
    if (!ok) std::printf("  boot_to_entry: %s\n", err.c_str());
    CHECK(ok);
    CHECK(r.import_count > 0);
    CHECK(r.entry_va == r.load_base + entry_rva);
    CHECK(cpu.reg(R_EIP) == r.entry_va);
    std::printf("  base=0x%08x entry=0x%08x imports=%u sin_shim=%u\n",
                r.load_base, r.entry_va, r.import_count, r.unresolved_count);
    return true;
}

int main() {
    test_read_synthetic();
    test_rejects();
    test_boot_synthetic();
    bool real = test_real_exe();
    std::printf("%s (%d fallas)%s\n", g_fail ? "ROJO" : "VERDE", g_fail, real ? "" : " [A4 SKIP]");
    if (g_fail) return 1;
    return real ? 0 : 77;   // 77 = SKIP para CTest (SKIP_RETURN_CODE): A4 no corrió
}
