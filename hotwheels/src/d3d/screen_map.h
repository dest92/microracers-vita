#pragma once
#include <algorithm>
#include <cstdint>

namespace hw::d3d {

struct NativeScreenMap {
    int32_t x = 0, y = 0, w = 0, h = 0;
    float scale_x = 1.f, scale_y = 1.f;
    // Destination position of source pixel (0,0)'s center, relative to viewport.
    float first_x = 0.5f, first_y = 0.5f;
};

struct ClipTransform { float sx, sy, tx, ty; };
struct ScreenScissor { int32_t x, y, w, h; };

inline NativeScreenMap native_screen_map(uint32_t source_w, uint32_t source_h,
                                         int32_t screen_w, int32_t screen_h,
                                         bool stretch) {
    NativeScreenMap m;
    if (!source_w || !source_h || screen_w <= 0 || screen_h <= 0) return m;
    m.w = screen_w;
    m.h = screen_h;
    if (!stretch) {
        const int64_t source_aspect = int64_t(source_w) * screen_h;
        const int64_t output_aspect = int64_t(screen_w) * source_h;
        if (source_aspect > output_aspect)
            m.h = int(int64_t(screen_w) * source_h / source_w);
        else
            m.w = int(int64_t(screen_h) * source_w / source_h);
        m.x = (screen_w - m.w) / 2;
        m.y = (screen_h - m.h) / 2;
    }
    m.scale_x = float(m.w) / float(source_w);
    m.scale_y = float(m.h) / float(source_h);
    m.first_x = m.scale_x * 0.5f;
    m.first_y = m.scale_y * 0.5f;
    return m;
}

inline ClipTransform native_clip_transform(const NativeScreenMap& m,
                                           int32_t screen_w, int32_t screen_h) {
    const float sx = m.scale_x, sy = m.scale_y;
    return {2.f * sx / float(screen_w), -2.f * sy / float(screen_h),
            -1.f + 2.f * (float(m.x) + sx * 0.5f) / float(screen_w),
             1.f - 2.f * (float(m.y) + sy * 0.5f) / float(screen_h)};
}

inline ScreenScissor map_scissor(const NativeScreenMap& m, uint32_t source_w,
                                 uint32_t source_h, int32_t screen_h,
                                 int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!source_w || !source_h) return {0, 0, 0, 0};
    const auto clamp_axis = [](int64_t p, uint32_t limit) {
        return int32_t(std::max<int64_t>(0, std::min<int64_t>(limit, p)));
    };
    const int32_t sx0 = clamp_axis(x, source_w), sx1 = clamp_axis(int64_t(x) + w, source_w);
    const int32_t sy0 = clamp_axis(y, source_h), sy1 = clamp_axis(int64_t(y) + h, source_h);
    const int32_t x0 = m.x + int64_t(sx0) * m.w / source_w;
    const int32_t x1 = m.x + int64_t(sx1) * m.w / source_w;
    const int32_t y0 = int64_t(sy0) * m.h / source_h;
    const int32_t y1 = int64_t(sy1) * m.h / source_h;
    return {x0, screen_h - (m.y + int(y1)), std::max(0, x1 - x0), std::max(0, int(y1 - y0))};
}

} // namespace hw::d3d
