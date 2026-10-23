#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace winx86::dx::ddraw {
// Host-side values/objects. They are not PE32 layouts and must never be
// overlaid on or memcpy'd from guest structures.
constexpr uint32_t kOk = 0;
constexpr uint32_t kNoInterface = 0x80004002u;
constexpr uint32_t kInvalidParam = 0x80070057u;
constexpr uint32_t kUnsupported = 0x88760000u;
constexpr uint32_t kNoD3D = 0x887600AAu;
constexpr uint32_t kPrimary = 0x200, kBack = 0x4, kFlip = 0x10,
                   kComplex = 0x8, kOffscreen = 0x40, kSysmem = 0x800;
constexpr uint32_t kSrcColorKey = 0x1;
struct Rect { int32_t left, top, right, bottom; };
struct PaletteEntry { uint8_t red, green, blue, flags; };
struct LockInfo { uint32_t va; uint32_t pitch; uint32_t width; uint32_t height; };
struct Memory {
    virtual ~Memory() = default;
    // Guest VAs remain uint32_t. ptr validates/maps the entire requested span;
    // Device borrows the host pointer only through the enclosing Device call.
    virtual uint32_t alloc(size_t bytes) = 0;
    virtual uint8_t* ptr(uint32_t va, size_t bytes) = 0;
};
struct Presenter {
    virtual ~Presenter() = default;
    // Called synchronously; implementations must consume/copy pixels before return.
    virtual void present(const uint8_t* idx, uint32_t pitch, const uint32_t pal[256]) = 0;
    virtual void present_sized(const uint8_t* idx,uint32_t pitch,const uint32_t pal[256],
                               uint32_t,uint32_t) { present(idx,pitch,pal); }
};
struct Surface {
    uint32_t caps = 0, width = 0, height = 0, pitch = 0, va = 0, bpp = 8;
    Surface* attached = nullptr;
    bool locked = false, has_key = false;
    uint32_t key_low = 0, key_high = 0,generation=0;
    uint32_t red_mask=0xF800,green_mask=0x07E0,blue_mask=0x001F,alpha_mask=0;
};
class Device {
public:
    Device(Memory& mem, Presenter& presenter) : mem_(mem), presenter_(presenter) {}
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;
    Surface* create_surface(uint32_t caps, uint32_t width, uint32_t height, uint32_t back_count, uint32_t bpp=8);
    uint32_t lock(Surface& surface, LockInfo& out);
    uint32_t unlock(Surface& surface);
    uint32_t blt_fast(Surface& dst, uint32_t x, uint32_t y, Surface& src,
                      Rect rect, uint32_t flags);
    uint32_t blt(Surface& dst, Rect dst_rect, Surface& src, Rect src_rect, uint32_t flags);
    uint32_t flip(Surface& primary);
    void set_entries(uint32_t start, const PaletteEntry* entries, uint32_t count);
    const uint32_t* palette() const { return palette_.data(); }
    uint32_t create_device(uint32_t& out) const { out = 0; return kNoD3D; }
    Surface* primary() const { return primary_; }
    void reset_primary() {
        if (primary_) primary_->caps &= ~kPrimary;
        primary_ = nullptr;
    }
private:
    Surface* make_surface(uint32_t caps, uint32_t width, uint32_t height, uint32_t bpp);
    Memory& mem_;
    Presenter& presenter_;
    std::vector<std::unique_ptr<Surface>> surfaces_;
    Surface* primary_ = nullptr;
    std::array<uint32_t,256> palette_{};
};

// Layout de los slots ABI, contado desde IUnknown (0-based).
namespace slot {
constexpr unsigned dd_create_palette=5, dd_create_surface=6, dd_enum_modes=8,
                   dd_set_cooperative=20, dd_set_mode=21, dd_wait_vblank=22;
constexpr unsigned surf_blt=5, surf_blt_fast=7, surf_flip=11,
                   surf_get_attached=12, surf_lock=25, surf_set_key=29,
                   surf_set_palette=31, surf_unlock=32;
constexpr unsigned d3d_create_device=8, palette_set_entries=6;
}
}
