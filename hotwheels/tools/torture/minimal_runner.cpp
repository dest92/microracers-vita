// Minimal ARM-side consumer of libwinx86.
//
// It avoids the D2Vita PE loader, game shims and rt_boot.cpp: the contract
// under test is Cpu::map/write/invalidate_code/set_trap/run. Three guest runs
// exercise a translated loop, a changing indirect target, and SMC
// invalidation under both WX86_CHAIN modes.
#include "runtime/cpu.h"

#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t kCode = 0x10000000u;
constexpr uint32_t kTargetA = 0x10001000u;
constexpr uint32_t kTargetB = 0x10002000u;
constexpr uint32_t kTrapLo = 0x10003000u;
constexpr uint32_t kTrapHi = 0x10003100u;
constexpr uint32_t kStack = 0x10100000u;
constexpr uint32_t kData = 0x10200000u;

struct Observation { uint32_t eax = 0, ecx = 0, ebx = 0; bool trapped = false; };
} // namespace

int main() {
    // mov ecx,10; xor eax,eax; loop: inc eax; dec ecx; jnz loop; jmp edx
    static const uint8_t code[] = {
        0xB9, 0x0A, 0x00, 0x00, 0x00,
        0x31, 0xC0,
        0x40,
        0x49,
        0x75, 0xFC,
        0xFF, 0xE2,
    };
    static const uint8_t target_a[] = {
        0xBB, 0x11, 0x00, 0x00, 0x00, // mov ebx,0x11
        0x89, 0x1D, 0x00, 0x00, 0x20, 0x10, // mov [kData],ebx
        0xBA, 0x00, 0x30, 0x00, 0x10, // mov edx,trap
        0xFF, 0xE2,                   // jmp edx
    };
    static const uint8_t target_b[] = {
        0xBB, 0x22, 0x00, 0x00, 0x00, // mov ebx,0x22
        0x89, 0x1D, 0x00, 0x00, 0x20, 0x10,
        0xBA, 0x00, 0x30, 0x00, 0x10,
        0xFF, 0xE2,
    };

    d2rt::Cpu* cpu = d2rt::make_cpu_box86();
    if (!cpu) {
        std::fprintf(stderr, "minimal_runner: make_cpu_box86 failed\n");
        return 2;
    }
    if (!cpu->map(kCode, 0x1000, code, d2rt::P_RWX) ||
        !cpu->map(kTargetA, 0x1000, target_a, d2rt::P_RWX) ||
        !cpu->map(kTargetB, 0x1000, target_b, d2rt::P_RWX) ||
        !cpu->map(kStack, 0x2000, nullptr, d2rt::P_RWX) ||
        !cpu->map(kData, 0x1000, nullptr, d2rt::P_RWX)) {
        std::fprintf(stderr, "minimal_runner: guest map failed\n");
        return 3;
    }

    const char* label = "unset";
    Observation observed;
    cpu->set_trap(kTrapLo, kTrapHi, [&](d2rt::Cpu& state, uint32_t slot) {
        observed = {state.reg(d2rt::R_EAX), state.reg(d2rt::R_ECX),
                    state.reg(d2rt::R_EBX), true};
        std::printf("TORTURE|case=%s|slot=0x%08x|eax=%u|ecx=%u|ebx=%u|mem=%u|fault=none\n",
                    label, slot, observed.eax, observed.ecx, observed.ebx,
                    state.read_u32(kData));
        return false; // clean stop at the synthetic trap
    });

    auto run_case = [&](const char* name, uint32_t target, uint32_t want_ebx) {
        label = name;
        observed = {};
        cpu->write_u32(kData, 0);
        cpu->set_reg(d2rt::R_ESP, kStack + 0x2000 - 16);
        cpu->set_reg(d2rt::R_EAX, 0);
        cpu->set_reg(d2rt::R_ECX, 0);
        cpu->set_reg(d2rt::R_EDX, target);
        const char* fault = nullptr;
        const bool clean = cpu->run(kCode, &fault);
        const uint32_t memory = cpu->read_u32(kData);
        if (!clean || !observed.trapped || observed.eax != 10 ||
            observed.ecx != 0 || observed.ebx != want_ebx || memory != want_ebx) {
            std::fprintf(stderr,
                "minimal_runner: case=%s clean=%u trap=%u eax=%u ecx=%u ebx=%u mem=%u fault=%s addr=0x%08x code=0x%08x\n",
                name, clean ? 1u : 0u, observed.trapped ? 1u : 0u,
                observed.eax, observed.ecx, observed.ebx, memory,
                fault ? fault : "none", cpu->fault_addr(), cpu->fault_code());
            return false;
        }
        return true;
    };

    if (!run_case("indirect-a", kTargetA, 0x11) ||
        !run_case("indirect-b", kTargetB, 0x22)) return 4;
    const uint32_t smc_value = 0x33;
    if (!cpu->write(kTargetB + 1, &smc_value, sizeof smc_value)) {
        std::fprintf(stderr, "minimal_runner: guest SMC write failed\n");
        return 5;
    }
    // Cpu::write participates in SMC tracking; the explicit discard also
    // exercises the public invalidate contract used by FlushInstructionCache.
    cpu->invalidate_code(kTargetB, sizeof target_b);
    if (!run_case("smc-retranslated", kTargetB, 0x33)) return 6;
    return 0;
}
