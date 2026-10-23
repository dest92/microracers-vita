#include "../../include/winx86-dx/d3d/device.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace winx86::dx::d3d {
namespace {
uint32_t bytes_per_pixel(TexFormat format) {
    switch (format) {
    case TexFormat::ARGB1555: case TexFormat::RGB565: case TexFormat::ARGB4444: return 2;
    case TexFormat::ARGB8888: return 4;
    case TexFormat::PAL8: return 1;
    }
    return 0;
}
} // namespace

void Device::note(const std::string& message) {
    if (noted_[message]) return;
    noted_[message] = true;
    if (log) log(message);
}

uint32_t Device::begin_scene() {
    if (in_scene_) return kSceneInScene;
    in_scene_ = true;
    sink_.begin_scene(w_, h_);
    return kOk;
}

uint32_t Device::end_scene() {
    if (!in_scene_) return kSceneNotInScene;
    in_scene_ = false;
    sink_.end_scene();
    return kOk;
}

uint32_t Device::set_render_state(uint32_t state, uint32_t value) {
    switch (state) {
    case RS_CULLMODE:
        if (value != 1) note("CULLMODE != NONE ignorado");
        return kOk;
    case RS_TEXTUREMAG: case RS_TEXTUREMIN: case RS_SRCBLEND: case RS_DESTBLEND:
    case RS_ALPHABLENDENABLE: case RS_COLORKEYENABLE:
        sink_.set_render_state(state, value);
        return kOk;
    default:
        note("SetRenderState no soportado: " + std::to_string(state));
        return kOk;
    }
}

uint32_t Device::set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value) {
    if (stage != 0 || type != 1 /* COLOROP */) {
        note("TSS ignorado: tipo " + std::to_string(type));
        return kOk;
    }
    if (value != 2 && value != 4) note("COLOROP no soportado: " + std::to_string(value));
    sink_.set_texture_stage_state(stage, type, value);
    return kOk;
}

uint32_t Device::set_texture(uint32_t stage, uint32_t handle) {
    if (stage != 0) {
        note("SetTexture stage > 0 ignorado");
        return kOk;
    }
    if (handle) {
        TextureSource source;
        if (!lookup_ || !lookup_(handle, source) || !source.pixels || !source.width || !source.height)
            return kInvalidParams;
        const uint32_t bpp = bytes_per_pixel(source.format);
        const uint64_t row64 = uint64_t(source.width) * bpp;
        if (!bpp || row64 > std::numeric_limits<uint32_t>::max() || source.pitch < row64)
            return kInvalidParams;
        if (row64 > std::numeric_limits<uint64_t>::max() / source.height) return kInvalidParams;
        const uint64_t bytes64 = row64 * source.height;
        const uint64_t preceding_rows = source.height - 1;
        if (preceding_rows && uint64_t(source.pitch) >
            (std::numeric_limits<uint64_t>::max() - row64) / preceding_rows) return kInvalidParams;
        const uint64_t source_span = uint64_t(source.pitch) * preceding_rows + row64;
        if (bytes64 > std::numeric_limits<size_t>::max() ||
            source_span > std::numeric_limits<size_t>::max()) return kInvalidParams;

        const auto existing = uploaded_generation_.find(handle);
        if (existing == uploaded_generation_.end() || existing->second != source.generation) {
            TextureUpload upload;
            upload.handle = handle;
            upload.width = source.width;
            upload.height = source.height;
            upload.pitch = static_cast<uint32_t>(row64);
            upload.format = source.format;
            upload.has_key = source.has_key;
            upload.key_low = source.key_low;
            upload.key_high = source.key_high;
            upload.pixels.resize(static_cast<size_t>(bytes64));
            if (source.format == TexFormat::PAL8) {
                if (source.palette) upload.palette.assign(source.palette, source.palette + 256);
                else upload.palette.assign(256, 0xFF000000u);
            }
            for (uint32_t y = 0; y < source.height; ++y) {
                std::memcpy(upload.pixels.data() + size_t(y) * upload.pitch,
                            source.pixels + size_t(y) * source.pitch, upload.pitch);
            }
            sink_.upload_texture(upload);
            uploaded_generation_[handle] = source.generation;
        }
    }
    sink_.set_texture(stage, handle);
    return kOk;
}

uint32_t Device::draw_primitive(uint32_t primitive, uint32_t fvf, const float* host_vertices,
                                uint32_t count, uint32_t) {
    if (fvf != kFvfXyzrhwTex1 || primitive < 4 || primitive > 6 || count < 3 || !host_vertices ||
        count > std::numeric_limits<size_t>::max() / 6) return kInvalidParams;
    if (!in_scene_) return kSceneNotInScene;
    std::vector<DrawVertex> vertices;
    vertices.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const float* v = host_vertices + size_t(i) * 6;
        vertices.push_back({v[0], v[1], v[2], v[3], v[4], v[5]});
    }
    sink_.draw_primitive(primitive, vertices);
    return kOk;
}

uint32_t Device::create_viewport() {
    const uint32_t id = next_viewport_++;
    viewports_[id] = {0, 0, static_cast<int32_t>(w_), static_cast<int32_t>(h_)};
    return id;
}

uint32_t Device::set_viewport2(uint32_t viewport, int32_t x, int32_t y, int32_t w, int32_t h) {
    const auto it = viewports_.find(viewport);
    if (it == viewports_.end() || w <= 0 || h <= 0) return kInvalidParams;
    it->second = {x, y, w, h};
    return kOk;
}

uint32_t Device::set_current_viewport(uint32_t viewport) {
    const auto it = viewports_.find(viewport);
    if (it == viewports_.end()) return kInvalidParams;
    sink_.set_viewport(it->second);
    return kOk;
}

uint32_t Device::viewport_clear(uint32_t viewport, uint32_t count, const Rect* host_rects, uint32_t flags) {
    const auto it = viewports_.find(viewport);
    if (it == viewports_.end()) return kInvalidParams;
    if (!(flags & 1)) return kOk; // only D3DCLEAR_TARGET (no Z buffer)
    const Rect& view = it->second;
    const auto clear_intersection = [&](Rect rect) {
        const int64_t x0 = std::max<int64_t>(rect.x, view.x);
        const int64_t y0 = std::max<int64_t>(rect.y, view.y);
        const int64_t x1 = std::min<int64_t>(int64_t(rect.x) + rect.w, int64_t(view.x) + view.w);
        const int64_t y1 = std::min<int64_t>(int64_t(rect.y) + rect.h, int64_t(view.y) + view.h);
        if (x1 > x0 && y1 > y0)
            sink_.clear({static_cast<int32_t>(x0), static_cast<int32_t>(y0),
                         static_cast<int32_t>(x1 - x0), static_cast<int32_t>(y1 - y0)}, 0xFF000000u);
    };
    if (count == 0) clear_intersection(view);
    for (uint32_t i = 0; i < count && host_rects; ++i) clear_intersection(host_rects[i]);
    return kOk;
}

void Device::present() { sink_.present(); }

void Device::forget_texture(uint32_t handle) {
    if (uploaded_generation_.erase(handle)) sink_.free_texture(handle);
}

std::vector<TexFormat> Device::texture_formats() {
    return {TexFormat::ARGB1555, TexFormat::RGB565, TexFormat::ARGB4444, TexFormat::PAL8};
}

} // namespace winx86::dx::d3d
