// fake_cpu.h — CPU invitada falsa para tests host (memoria por regiones +
// registros, registra protecciones). No ejecuta x86.
#pragma once
#include "runtime/cpu.h"

#include <cstring>
#include <map>
#include <vector>

using d2rt::Cpu;
using d2rt::TrapFn;

// CPU falsa: memoria por regiones + registros. No ejecuta nada.
struct FakeCpu : Cpu {
    std::map<uint32_t, int> prot;   // base de región -> P_*
    std::map<uint32_t, std::vector<uint8_t>> mem;
    uint32_t regs[16] = {};
    uint32_t trap_lo = 0, trap_hi = 0;
    std::vector<uint8_t>* region(uint32_t va, uint32_t n, uint32_t& off) {
        auto it = mem.upper_bound(va);
        if (it == mem.begin()) return nullptr;
        --it;
        if (va - it->first + (uint64_t)n > it->second.size()) return nullptr;
        off = va - it->first;
        return &it->second;
    }
    bool map(uint32_t va, uint32_t size, const void* init, int p) override {
        auto& r = mem[va];
        r.assign(size, 0);
        prot[va] = p;
        if (init) std::memcpy(r.data(), init, size);
        return true;
    }
    bool protect(uint32_t, uint32_t, int) override { return true; }
    bool read(uint32_t va, void* dst, uint32_t n) override {
        uint32_t off; auto* r = region(va, n, off);
        if (!r) return false;
        std::memcpy(dst, r->data() + off, n); return true;
    }
    bool write(uint32_t va, const void* src, uint32_t n) override {
        uint32_t off; auto* r = region(va, n, off);
        if (!r) return false;
        std::memcpy(r->data() + off, src, n); return true;
    }
    bool mapped(uint32_t va) override { uint32_t off; return region(va, 1, off) != nullptr; }
    uint32_t reg(int r) override { return regs[r]; }
    void set_reg(int r, uint32_t v) override { regs[r] = v; }
    void set_trap(uint32_t lo, uint32_t hi, TrapFn) override { trap_lo = lo; trap_hi = hi; }
    bool run(uint32_t, const char** fault) override { if (fault) *fault = "FakeCpu no ejecuta"; return false; }
    uint32_t fault_addr() const override { return 0; }
};

