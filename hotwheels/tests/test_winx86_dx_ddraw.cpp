#include "winx86-dx/ddraw/software.h"
#include "winx86-dx/ddraw/abi.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <type_traits>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

using namespace winx86::dx::ddraw;

static_assert(slot::dd_create_surface == 6 && slot::surf_lock == 25);
static_assert(abi::ddraw4.size() == 28 && abi::surface4.size() == 45);
static_assert(std::is_same_v<decltype(Surface::va), uint32_t>);
static_assert(!std::is_copy_constructible_v<Device> && !std::is_move_constructible_v<Device>);

struct TestMemory final : Memory {
    uint32_t next = 0x10000000;
    std::map<uint32_t, std::vector<uint8_t>> blocks;

    uint32_t alloc(size_t bytes) override {
        const uint32_t va = next;
        next += uint32_t((bytes + 4095) & ~size_t(4095));
        blocks[va].resize(bytes);
        return va;
    }

    uint8_t* ptr(uint32_t va, size_t bytes) override {
        auto it = blocks.upper_bound(va);
        if (it == blocks.begin()) return nullptr;
        --it;
        const size_t offset = va - it->first;
        return offset + bytes <= it->second.size() ? it->second.data() + offset : nullptr;
    }
};

struct Capture final : Presenter {
    unsigned calls = 0;
    uint32_t first = 0;
    void present(const uint8_t* pixels, uint32_t, const uint32_t palette[256]) override {
        ++calls;
        first = palette[pixels[0]];
    }
};

int main() {
    TestMemory memory;
    Capture presenter;
    Device device(memory, presenter);
    Surface* primary = device.create_surface(kPrimary | kComplex | kFlip, 2, 1, 1);
    CHECK(primary && primary->attached);
    const int32_t i32min = std::numeric_limits<int32_t>::min();
    const int32_t i32max = std::numeric_limits<int32_t>::max();
    const uint32_t extreme_blt = device.blt(*primary, {i32min, i32min, i32max, i32max}, *primary,
                                            {i32min, i32min, i32max, i32max}, 0);
    CHECK(extreme_blt == kOk);
    LockInfo lock{};
    CHECK(device.lock(*primary->attached, lock) == kOk);
    memory.ptr(lock.va, 2)[0] = 3;
    CHECK(device.unlock(*primary->attached) == kOk);

    const PaletteEntry color{0x12, 0x34, 0x56, 0};
    device.set_entries(3, &color, 1);
    CHECK(device.flip(*primary) == kOk);
    CHECK(presenter.calls == 1 && presenter.first == 0xFF123456u);
}
