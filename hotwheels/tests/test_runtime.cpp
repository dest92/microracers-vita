// test_runtime.cpp — criterios B1–B4 de specs/002-vita-runtime.md (host).
#include "boot/hw_boot.h"
#include "runtime/hw_layout.h"
#include "runtime/bridge.h"
#include "fake_cpu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace d2rt;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static const uint32_t kExeBase = 0x00400000, kExeSize = 0x79000;   // HotWheels.exe (pe_analyze)

static bool overlap(const hw::Region& a, const hw::Region& b) {
    return (uint64_t)a.base < (uint64_t)b.base + b.size && (uint64_t)b.base < (uint64_t)a.base + a.size;
}

static void test_layout_ok() {   // B1
    hw::Layout l = hw::make_layout(kExeBase, kExeSize);
    CHECK(l.ok);
    if (!l.ok) { std::printf("  why: %s\n", l.why.c_str()); return; }
    auto rs = l.regions();
    CHECK(rs.size() == 8);
    hw::Region exe{"exe", kExeBase, kExeSize};
    for (size_t i = 0; i < rs.size(); ++i) {
        CHECK(rs[i].size > 0);
        CHECK(rs[i].base >= 0x1000);
        CHECK((uint64_t)rs[i].base + rs[i].size <= (uint64_t)l.arena - 0x1000000);
        CHECK(!overlap(rs[i], exe));
        for (size_t j = i + 1; j < rs.size(); ++j)
            if (overlap(rs[i], rs[j])) { std::printf("  solape %s/%s\n", rs[i].name, rs[j].name); ++g_fail; }
    }
    CHECK(l.main_stack_top == l.main_stack.base + l.main_stack.size);
    CHECK(l.main_stack.size == 0x200000);   // = stack_size_ del Bridge (2 MiB)
    CHECK(l.traps.size == 0x100000);        // = extensión de trampas que deriva el Bridge
    CHECK(l.span <= l.arena - 0x1000000);
    std::printf("  layout: span=0x%08x arena=0x%08x (%u MiB)\n", l.span, l.arena, l.arena >> 20);
}

static void test_layout_rejects_big_exe() {   // B2
    hw::Layout l = hw::make_layout(kExeBase, 0x200000);
    CHECK(!l.ok);
    CHECK(!l.why.empty());
}

// PE32 mínimo sin relocs en 0x400000 con un import (KERNEL32!ExitProcess).
static std::vector<uint8_t> tiny_pe() {
    std::vector<uint8_t> f(0x600, 0);
    auto w16 = [&](size_t o, uint16_t v) { std::memcpy(&f[o], &v, 2); };
    auto w32 = [&](size_t o, uint32_t v) { std::memcpy(&f[o], &v, 4); };
    f[0] = 'M'; f[1] = 'Z'; w32(0x3c, 0x40); std::memcpy(&f[0x40], "PE\0\0", 4);
    const size_t coff = 0x44, opt = coff + 20, sec = opt + 224;
    w16(coff, 0x14c); w16(coff + 2, 2); w16(coff + 16, 224); w16(coff + 18, 0x0103);   // relocs stripped
    w16(opt, 0x10b); w32(opt + 16, 0x1000); w32(opt + 28, kExeBase); w32(opt + 32, 0x1000); w32(opt + 36, 0x200);
    w32(opt + 56, 0x3000); w32(opt + 60, 0x200); w16(opt + 68, 2); w32(opt + 92, 16);
    w32(opt + 96 + 8, 0x2000); w32(opt + 96 + 12, 0x80);
    std::memcpy(&f[sec], ".text", 5); w32(sec + 8, 0x200); w32(sec + 12, 0x1000); w32(sec + 16, 0x200); w32(sec + 20, 0x200);
    std::memcpy(&f[sec + 40], ".idata", 6); w32(sec + 48, 0x200); w32(sec + 52, 0x2000); w32(sec + 56, 0x200); w32(sec + 60, 0x400);
    f[0x200] = 0xc3;
    auto R = [](uint32_t rva) { return (size_t)(rva - 0x2000 + 0x400); };
    w32(R(0x2000), 0x2040); w32(R(0x2000) + 12, 0x2060); w32(R(0x2000) + 16, 0x2050);
    w32(R(0x2040), 0x2070); w32(R(0x2050), 0x2070);
    std::memcpy(&f[R(0x2060)], "KERNEL32.dll", 12); std::memcpy(&f[R(0x2072)], "ExitProcess", 11);
    return f;
}

static void test_bridge_follows_layout() {   // B3
    hw::Layout l = hw::make_layout(kExeBase, kExeSize);
    if (!l.ok) { ++g_fail; return; }
    auto env = hw::layout_env(l);
    bool has_arena = false;
    for (auto& kv : env) if (kv.first == "WX86_ARENA") has_arena = true;
    CHECK(has_arena);
    hw::apply_layout_env(l);
    FakeCpu cpu; Bridge br(&cpu);
    CHECK(br.stack_top() == l.main_stack_top);
    hw::BootOptions opt; opt.log_unresolved = false; hw::BootResult r; std::string err;
    bool ok = hw::boot_to_entry(br, cpu, tiny_pe(), opt, r, err);
    if (!ok) std::printf("  boot_to_entry: %s\n", err.c_str());
    CHECK(ok);
    CHECK(r.load_base == kExeBase);
    uint32_t slot = cpu.read_u32(kExeBase + 0x2050);
    CHECK(slot >= l.traps.base && slot < l.traps.base + l.traps.size);
    CHECK(r.sentinel >= l.traps.base && r.sentinel < l.traps.base + l.traps.size);
    for (auto& kv : env) unsetenv(kv.first.c_str());
}

static void test_map_runtime() {   // B4
    hw::Layout l = hw::make_layout(kExeBase, kExeSize);
    if (!l.ok) { ++g_fail; return; }
    FakeCpu cpu; std::string err;
    bool ok = hw::map_runtime(cpu, l, err);
    if (!ok) std::printf("  map_runtime: %s\n", err.c_str());
    CHECK(ok);
    auto has = [&](const hw::Region& r, int p) {
        auto it = cpu.mem.find(r.base);
        return it != cpu.mem.end() && it->second.size() == r.size && cpu.prot[r.base] == p;
    };
    CHECK(has(l.heap, P_RW));
    CHECK(has(l.va, P_RWX));
    CHECK(has(l.misc, P_RWX));
}

int main() {
    test_layout_ok();
    test_layout_rejects_big_exe();
    test_bridge_follows_layout();
    test_map_runtime();
    std::printf("%s (%d fallas)\n", g_fail ? "ROJO" : "VERDE", g_fail);
    return g_fail ? 1 : 0;
}
