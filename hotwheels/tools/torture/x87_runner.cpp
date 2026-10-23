#include "runtime/cpu.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int dyn86_tcache_code_stable(const uint8_t*, uint32_t, uint32_t, uint32_t);
extern "C" uintptr_t dyn86_membase;
extern "C" int dyn86_tcache_apply_fixups(uint8_t*, uint32_t, const void*, uint32_t);
struct dyn86_tcache_view_s {
    uint32_t arm_size, inst_count, flags, instsize_size, reloc_count;
    const uint8_t* arm;
    const uint8_t* instsize;
    const void* relocs;
};
extern "C" int dyn86_tcache_find(uint32_t, const void*, uint32_t, dyn86_tcache_view_s*);
extern "C" void dyn86_tcache_record(uint32_t, const void*, uint32_t, const uint8_t*, uint32_t,
                                     const void*, uint32_t, uint32_t, uint32_t);

namespace {
constexpr uint32_t kCode = 0x10000000u, kStack = 0x10100000u;
constexpr uint32_t kInput = 0x10200000u, kOutput = 0x10201000u;
constexpr uint32_t kTrap = 0x10003000u;
struct Result { uint64_t value; uint16_t status; uint16_t pad; };
struct Input { uint64_t bits; uint16_t cw; uint16_t pad; uint32_t reserved; };
struct Test { const char* name; uint64_t bits; uint16_t cw; };
const Test tests[] = {
    {"pos-half-nearest-even", 0x3fe0000000000000ULL, 0x037f},
    {"neg-one-point-five-nearest-even", 0xbff8000000000000ULL, 0x037f},
    {"pos-one-half-down", 0x3fe0000000000000ULL, 0x077f},
    {"neg-one-half-up", 0xbfe0000000000000ULL, 0x0b7f},
    {"trunc-pos-fraction", 0x400e000000000000ULL, 0x0f7f},
    {"trunc-neg-fraction", 0xc00e000000000000ULL, 0x0f7f},
    {"int32-max", 0x41dfffffffc00000ULL, 0x0f7f},
    {"int32-min", 0xc1e0000000000000ULL, 0x0f7f},
    {"int32-overflow", 0x41e0000000000000ULL, 0x0f7f},
    {"small-denormal", 0x0000000000000001ULL, 0x0f7f},
    {"positive-infinity", 0x7ff0000000000000ULL, 0x0f7f},
    {"quiet-nan", 0x7ff8000000000001ULL, 0x0f7f},
    {"exact-fild-fistp", 0x0000000180000001ULL, 0x037f},
};
}

int main(int argc, char** argv) {
    const uint32_t local_pc_branch[] = {0xea000000u, 0xe1a00000u, 0xe1a00000u};
    const uint32_t external_pc_branch[] = {0xea00000au, 0xe1a00000u, 0xe1a00000u};
    const uint32_t blx_immediate[] = {0xfa000000u, 0xe1a00000u, 0xe1a00000u};
    const uint32_t pc_literal[] = {0xe59f0000u, 0xe1a00000u, 0xe1a00000u};
    const uint32_t vfp_pc_literal[] = {0xed9f0b00u, 0xe1a00000u, 0xe1a00000u};
    const uint32_t unrecognized_pc_address[] = {0xe28f0000u, 0xe1a00000u, 0xe1a00000u};
    const uint32_t armv7_ubfx[] = {0xe7ef305eu};
    if (!dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(local_pc_branch), sizeof(local_pc_branch), 0, 0) ||
        dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(external_pc_branch), sizeof(external_pc_branch), 0, 0) ||
        dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(blx_immediate), sizeof(blx_immediate), 0, 0) ||
        dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(pc_literal), sizeof(pc_literal), 0, 0) ||
        dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(vfp_pc_literal), sizeof(vfp_pc_literal), 0, 0) ||
        dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(unrecognized_pc_address), sizeof(unrecognized_pc_address), 0, 0) ||
        !dyn86_tcache_code_stable(reinterpret_cast<const uint8_t*>(armv7_ubfx), sizeof(armv7_ubfx), 0, 0)) {
        std::fprintf(stderr, "x87_runner: ARM cache eligibility checks failed\n");
        return 8;
    }
    struct Reloc { uint32_t off, type, value; };
    uint32_t branch_body[] = {0xea000000u, 0xe1a00000u, 0xe1a00000u, 0xe1a00000u};
    const Reloc branch_fixup{0, 1, 12};
    if (!dyn86_tcache_apply_fixups(reinterpret_cast<uint8_t*>(branch_body), sizeof(branch_body), &branch_fixup, 1) ||
        branch_body[0] != 0xea000001u) return 10;
    uint32_t host_base_add = 0xe2881441u; // ADD r1,r8,#0x41000000
    const Reloc host_fixup{0, 2, 0};
    uintptr_t old_membase = dyn86_membase;
    dyn86_membase = 0x42000000u;
    bool host_fixup_ok = dyn86_tcache_apply_fixups(reinterpret_cast<uint8_t*>(&host_base_add), 4, &host_fixup, 1);
    dyn86_membase = old_membase;
    uint32_t encoded = host_base_add & 0xfffu;
    unsigned rotate = ((encoded >> 8) & 15u) * 2u;
    uint32_t decoded = encoded & 255u;
    decoded = rotate ? (decoded >> rotate) | (decoded << (32u - rotate)) : decoded;
    if (!host_fixup_ok || decoded != 0x42000000u) return 11;
    dyn86_membase = 0x12345678u;
    bool unencodable_ok = !dyn86_tcache_apply_fixups(reinterpret_cast<uint8_t*>(&host_base_add), 4, &host_fixup, 1);
    dyn86_membase = old_membase;
    if (!unencodable_ok || host_base_add != 0xe2881442u) return 12;
    if (argc != 2) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> code((std::istreambuf_iterator<char>(f)), {});
    if (!f || code.empty()) return 2;
    auto* cpu = d2rt::make_cpu_box86();
    if (!cpu) return 3;
    const uint8_t cache_source[] = {0x43, 0x41, 0x43, 0x48};
    const uint32_t safe_arm[] = {0xea000001u, 0xe2881441u, 0xe1a00000u, 0xe12fff1eu};
    const uint8_t safe_instsize[] = {0, 0};
    dyn86_tcache_view_s cached{};
    uintptr_t record_base = dyn86_membase;
    bool persisted_hit = dyn86_tcache_find(0xf0000000u, cache_source, sizeof(cache_source), &cached);
    if (!persisted_hit) {
        dyn86_membase = 0x41000000u;
        dyn86_tcache_record(0xf0000000u, cache_source, sizeof(cache_source),
                            reinterpret_cast<const uint8_t*>(safe_arm), sizeof(safe_arm), safe_instsize,
                            0, sizeof(safe_instsize), 0);
        dyn86_membase = record_base;
    }
    if (persisted_hit) {
        if (cached.arm_size != sizeof(safe_arm) || cached.instsize_size != sizeof(safe_instsize) ||
            cached.reloc_count != 2 || cached.arm[0] != 0x01 || cached.arm[1] != 0x00 ||
            std::memcmp(cached.instsize, safe_instsize, sizeof(safe_instsize)) != 0) return 9;
        uint8_t restored[sizeof(safe_arm)];
        std::memcpy(restored, cached.arm, sizeof(restored));
        uintptr_t load_base = dyn86_membase; dyn86_membase = 0x42000000u;
        bool restore_ok = dyn86_tcache_apply_fixups(restored, sizeof(restored), cached.relocs, cached.reloc_count);
        dyn86_membase = load_base;
        uint32_t restored_words[4]; std::memcpy(restored_words, restored, sizeof(restored_words));
        if (!restore_ok || restored_words[0] != 0xea000001u || restored_words[1] != 0xe2881442u) return 13;
    }
    if (!cpu->map(kCode, 0x1000, code.data(), d2rt::P_RWX) ||
        !cpu->map(kStack, 0x2000, nullptr, d2rt::P_RWX) ||
        !cpu->map(kInput, 0x1000, nullptr, d2rt::P_RWX) ||
        !cpu->map(kOutput, 0x1000, nullptr, d2rt::P_RWX)) return 3;
    std::vector<Input> inputs;
    std::vector<Result> outputs(sizeof tests / sizeof tests[0]);
    for (const auto& t : tests) inputs.push_back({t.bits, t.cw, 0, 0});
    if (!cpu->write(kInput, inputs.data(), inputs.size() * sizeof(Input)) ||
        !cpu->write(kOutput, outputs.data(), outputs.size() * sizeof(Result))) return 4;
    bool trapped = false;
    cpu->set_trap(kTrap, kTrap + 0x100, [&](d2rt::Cpu&, uint32_t) {
        trapped = true;
        return false;
    });
    const uint32_t esp = kStack + 0x1f00;
    const uint32_t args[] = {kTrap, kInput, kOutput};
    if (!cpu->write(esp, args, sizeof args)) return 5;
    cpu->set_reg(d2rt::R_ESP, esp);
    const char* fault = nullptr;
    if (!cpu->run(kCode, &fault) || !trapped) {
        std::fprintf(stderr, "x87_runner: fault=%s addr=%08x code=%08x\n",
                     fault ? fault : "none", cpu->fault_addr(), cpu->fault_code());
        return 6;
    }
    if (!cpu->read(kOutput, outputs.data(), outputs.size() * sizeof(Result))) return 7;
    for (size_t i = 0; i < outputs.size(); ++i)
        std::printf("%s|%016llx|%04x\n", tests[i].name,
                    static_cast<unsigned long long>(outputs[i].value), outputs[i].status);
    return 0;
}
