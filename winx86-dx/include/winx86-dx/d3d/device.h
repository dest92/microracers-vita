#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace winx86::dx::d3d {

constexpr uint32_t kOk = 0;
constexpr uint32_t kInvalidParams = 0x80070057u;
constexpr uint32_t kSceneInScene = 0x88760320u;
constexpr uint32_t kSceneNotInScene = 0x88760322u;

enum class TexFormat : uint32_t { ARGB1555 = 1, RGB565, ARGB4444, ARGB8888, PAL8 };
enum : uint32_t { RS_TEXTUREMAG = 17, RS_TEXTUREMIN = 18, RS_SRCBLEND = 19, RS_DESTBLEND = 20,
                  RS_CULLMODE = 22, RS_ALPHABLENDENABLE = 27, RS_COLORKEYENABLE = 41 };
constexpr uint32_t kFvfXyzrhwTex1 = 0x104;

struct Rect { int32_t x = 0, y = 0, w = 0, h = 0; };
struct DrawVertex { float x = 0, y = 0, z = 0, rhw = 1, u = 0, v = 0; };

// Borrowed host view of a texture. Pixel/palette storage must remain valid
// through the enclosing Device::set_texture call; Device snapshots it before return.
struct TextureSource {
    uint32_t width = 0, height = 0, pitch = 0, generation = 0;
    TexFormat format = TexFormat::ARGB1555;
    // pixels spans pitch*(height-1) + width*bytes_per_pixel bytes; PAL8 palette
    // points to 256 entries when non-null. Both pointers are host addresses.
    const uint8_t* pixels = nullptr;
    const uint32_t* palette = nullptr; // PAL8: 256 ARGB entries
    bool has_key = false;
    uint32_t key_low = 0, key_high = 0;
};
using TextureLookup = std::function<bool(uint32_t handle, TextureSource& out)>;

struct TextureUpload {
    uint32_t handle = 0, width = 0, height = 0, pitch = 0;
    TexFormat format = TexFormat::ARGB1555;
    std::vector<uint8_t> pixels;        // tightly packed rows, owned snapshot
    std::vector<uint32_t> palette;      // PAL8: 256 ARGB entries
    bool has_key = false;
    uint32_t key_low = 0, key_high = 0;
};

// Receives semantic D3D operations, not the consumer's command wire format.
// Each call is synchronous; implementations copy values/payloads before return
// and must not retain references or pointers passed to them.
class CommandSink {
public:
    virtual ~CommandSink() = default;
    virtual void begin_scene(uint32_t width, uint32_t height) = 0;
    virtual void end_scene() = 0;
    virtual void set_render_state(uint32_t state, uint32_t value) = 0;
    virtual void set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value) = 0;
    virtual void set_texture(uint32_t stage, uint32_t handle) = 0;
    virtual void upload_texture(const TextureUpload& texture) = 0;
    virtual void draw_primitive(uint32_t primitive, const std::vector<DrawVertex>& vertices) = 0;
    virtual void set_viewport(Rect viewport) = 0;
    virtual void clear(Rect rect, uint32_t argb) = 0;
    virtual void present() = 0;
    virtual void free_texture(uint32_t handle) = 0;
};

// Portable D3D6 device semantics. Sink and lookup are borrowed and must outlive
// this object. Calls are serialized by the bridge thread; no worker is created.
class Device {
public:
    Device(CommandSink& sink, uint32_t target_w, uint32_t target_h,
           const TextureLookup& lookup) : sink_(sink), w_(target_w), h_(target_h), lookup_(lookup) {}
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    uint32_t begin_scene();
    uint32_t end_scene();
    uint32_t set_render_state(uint32_t state, uint32_t value);
    uint32_t set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value);
    uint32_t set_texture(uint32_t stage, uint32_t handle);
    // host_vertices has count consecutive groups of six floats (x,y,z,rhw,u,v).
    uint32_t draw_primitive(uint32_t primitive, uint32_t fvf, const float* host_vertices,
                            uint32_t count, uint32_t flags);
    uint32_t create_viewport();
    uint32_t set_viewport2(uint32_t viewport, int32_t x, int32_t y, int32_t w, int32_t h);
    uint32_t set_current_viewport(uint32_t viewport);
    // host_rects has count host Rect elements when non-null; never a guest wire layout.
    uint32_t viewport_clear(uint32_t viewport, uint32_t count, const Rect* host_rects, uint32_t flags);
    void present();
    void forget_texture(uint32_t handle);
    static std::vector<TexFormat> texture_formats();

    std::function<void(const std::string&)> log;

private:
    void note(const std::string& message);
    CommandSink& sink_;
    uint32_t w_, h_;
    const TextureLookup& lookup_;
    bool in_scene_ = false;
    std::map<uint32_t, Rect> viewports_;
    uint32_t next_viewport_ = 1;
    std::map<uint32_t, uint32_t> uploaded_generation_;
    std::map<std::string, bool> noted_;
};

} // namespace winx86::dx::d3d
