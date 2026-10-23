#include "winx86-dx/d3d/device.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

using namespace winx86::dx::d3d;

static_assert(!std::is_copy_constructible_v<Device> && !std::is_move_constructible_v<Device>);

struct RecordingSink final : CommandSink {
    uint32_t frame_w = 0, frame_h = 0;
    uint32_t render_state = 0, render_value = 0;
    uint32_t texture_handle = 0;
    uint32_t viewport_count = 0, clear_count = 0, present_count = 0, free_count = 0;
    Rect viewport{};
    Rect clear_rect{};
    uint32_t clear_argb = 0;
    uint32_t draw_prim = 0;
    std::vector<DrawVertex> draw_vertices;
    std::vector<TextureUpload> uploads;
    unsigned begin_count = 0, end_count = 0;

    void begin_scene(uint32_t w, uint32_t h) override { ++begin_count; frame_w = w; frame_h = h; }
    void end_scene() override { ++end_count; }
    void set_render_state(uint32_t s, uint32_t v) override { render_state = s; render_value = v; }
    void set_texture_stage_state(uint32_t, uint32_t, uint32_t) override {}
    void set_texture(uint32_t, uint32_t h) override { texture_handle = h; }
    void upload_texture(const TextureUpload& t) override { uploads.push_back(t); }
    void draw_primitive(uint32_t prim, const std::vector<DrawVertex>& v) override {
        draw_prim = prim;
        draw_vertices = v;
    }
    void set_viewport(Rect r) override { ++viewport_count; viewport = r; }
    void clear(Rect r, uint32_t argb) override { ++clear_count; clear_rect = r; clear_argb = argb; }
    void present() override { ++present_count; }
    void free_texture(uint32_t) override { ++free_count; }
};

int main() {
    RecordingSink sink;
    std::array<uint8_t, 4> pixels{1, 2, 3, 4};
    uint32_t lookup_generation = 7;
    TextureLookup lookup = [&](uint32_t handle, TextureSource& out) {
        if (handle != 11) return false;
        out.width = 2;
        out.height = 2;
        out.pitch = 2;
        out.generation = lookup_generation;
        out.format = TexFormat::PAL8;
        out.pixels = pixels.data();
        return true;
    };
    Device device(sink, 320, 200, lookup);

    CHECK(device.begin_scene() == kOk);
    CHECK(sink.begin_count == 1 && sink.frame_w == 320 && sink.frame_h == 200);
    CHECK(device.begin_scene() == kSceneInScene);
    CHECK(device.set_render_state(RS_ALPHABLENDENABLE, 1) == kOk);
    CHECK(sink.render_state == RS_ALPHABLENDENABLE && sink.render_value == 1);

    CHECK(device.set_texture(0, 11) == kOk);
    CHECK(sink.uploads.size() == 1 && sink.uploads[0].pixels == std::vector<uint8_t>({1, 2, 3, 4}));
    pixels[0] = 9;
    CHECK(sink.uploads[0].pixels[0] == 1); // API owns a synchronous snapshot, not the borrowed source.
    CHECK(device.set_texture(0, 11) == kOk && sink.uploads.size() == 1);
    ++lookup_generation;
    CHECK(device.set_texture(0, 11) == kOk && sink.uploads.size() == 2);
    CHECK(sink.uploads[1].pixels[0] == 9);

    const float vertices[18] = {0, 0, 0, 1, 0, 0, 8, 0, 0, 1, 1, 0, 0, 8, 0, 1, 0, 1};
    CHECK(device.draw_primitive(4, kFvfXyzrhwTex1, vertices, 3, 0) == kOk);
    CHECK(sink.draw_prim == 4 && sink.draw_vertices.size() == 3 && sink.draw_vertices[1].u == 1.0f);
    const uint32_t viewport = device.create_viewport();
    CHECK(device.set_viewport2(viewport, 1, 2, 10, 11) == kOk);
    CHECK(device.set_current_viewport(viewport) == kOk);
    CHECK(sink.viewport_count == 1 && sink.viewport.x == 1 && sink.viewport.h == 11);
    const Rect clear{0, 0, 5, 5};
    CHECK(device.viewport_clear(viewport, 1, &clear, 1) == kOk);
    CHECK(sink.clear_count == 1 && sink.clear_rect.x == 1 && sink.clear_rect.w == 4);
    CHECK(device.end_scene() == kOk && sink.end_count == 1);
    CHECK(device.end_scene() == kSceneNotInScene);
    device.present();
    device.forget_texture(11);
    CHECK(sink.present_count == 1 && sink.free_count == 1);
}
