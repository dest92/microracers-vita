#include "fps/interp60.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace hw::d3d;
using namespace hw::fps;

static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, sizeof u); return u; }
static float as_float(uint32_t u) { float f; std::memcpy(&f, &u, sizeof f); return f; }

static void emit_draw(Ring& r, uint32_t tex, float x, uint32_t prim = 5, uint32_t n = 4) {
    r.put(Op::Texture, {0, tex});
    std::vector<uint32_t> p = {prim, n};
    for (uint32_t i = 0; i < n; ++i) {
        const float vx = x + (i & 1 ? 10.f : 0.f);
        const float vy = (i & 2 ? 10.f : 0.f);
        p.insert(p.end(), {bits(vx), bits(vy), bits(0.f), bits(1.f), bits(float(i & 1)), bits(float((i >> 1) & 1))});
    }
    r.put(Op::Draw, p.data(), uint32_t(p.size()));
}

static Ring frame(float x, uint32_t tex = 7, uint32_t prim = 5, uint32_t n = 4, bool upload = false) {
    Ring r;
    if (upload) r.put(Op::TexUpload, {tex});
    r.put(Op::Begin, {320, 240});
    r.put(Op::Viewport, {0, 0, 320, 240});
    emit_draw(r, tex, x, prim, n);
    r.put(Op::End, {});
    r.put(Op::Present, {});
    return r;
}

static Ring wide_frame(float x, uint32_t width = 800) {
    Ring r;
    r.put(Op::Begin, {width, 600});
    r.put(Op::Viewport, {0, 0, width, 600});
    emit_draw(r, 7, x);
    r.put(Op::End, {});
    r.put(Op::Present, {});
    return r;
}

static Ring texture_event_frame(uint32_t tex, float x) {
    Ring r;
    r.put(Op::TexUpload, {tex});
    r.put(Op::Begin, {320, 240});
    r.put(Op::Viewport, {0, 0, 320, 240});
    emit_draw(r, tex, x);
    r.put(Op::End, {});
    r.put(Op::Present, {});
    return r;
}

static Ring two_draw_frame(float x, int32_t viewport_x = 0) {
    Ring r;
    r.put(Op::Begin, {320, 240});
    r.put(Op::Viewport, {uint32_t(viewport_x), 0, 320, 240});
    emit_draw(r, 7, x);
    emit_draw(r, 7, x + 100.f);
    r.put(Op::End, {});
    r.put(Op::Present, {});
    return r;
}

struct DrawItem { uint32_t texture; float x; uint32_t primitive = 5, vertices = 4; };
static Ring pattern_frame(const std::vector<DrawItem>& items) {
    Ring r;
    r.put(Op::Begin, {320, 240});
    r.put(Op::Viewport, {0, 0, 320, 240});
    for (const auto& item : items)
        emit_draw(r, item.texture, item.x, item.primitive, item.vertices);
    r.put(Op::End, {});
    r.put(Op::Present, {});
    return r;
}

static Ring device_frame(float x, uint32_t prim = 5, uint32_t count = 4,
                         int32_t viewport_x = 0) {
    Ring r;
    Device d(r, 320, 240, {});
    const uint32_t vp = d.create_viewport();
    assert(d.begin_scene() == kOk);
    assert(d.set_viewport2(vp, viewport_x, 0, 320, 240) == kOk);
    assert(d.set_current_viewport(vp) == kOk);
    float v[6 * 4] = {};
    for (uint32_t i = 0; i < count; ++i) {
        v[i * 6 + 0] = x + float(i & 1) * 10.f;
        v[i * 6 + 1] = float(i & 2) * 5.f;
        v[i * 6 + 2] = 0.25f;
        v[i * 6 + 3] = 1.f;
        v[i * 6 + 4] = float(i & 1);
        v[i * 6 + 5] = float((i >> 1) & 1);
    }
    assert(d.draw_primitive(prim, kFvfXyzrhwTex1, v, count, 0) == kOk);
    assert(d.end_scene() == kOk);
    d.present();
    return r;
}

static float first_draw_x(const Ring& r) {
    float result = -9999.f;
    r.for_each([&](uint16_t op, const uint32_t* p, uint32_t n) {
        if (op == uint16_t(Op::Draw) && n >= 8) result = as_float(p[2]);
    });
    return result;
}

struct CountingBackend final : Backend {
    void begin_frame(uint32_t, uint32_t) override { ++begins; }
    void upload_texture(const TextureData&) override {}
    void free_texture(uint32_t) override {}
    void clear(const Rect&, uint32_t) override {}
    void draw(const DrawCall& d) override { if (!d.verts.empty()) last_x = d.verts[0].x; ++draws; }
    void end_frame() override { ++ends; }
    void present() override { ++presents; }
    uint32_t begins = 0, ends = 0, presents = 0, draws = 0;
    float last_x = 0.f;
};

int main() {
    // T-rate: trailing 1s detects a low-rate interval crossing a fixed boundary.
    {
        PresentationHistory history;
        for (uint64_t t = 16667; t <= 10000000; t += 16667)
            history.record(t); // nominal 60 Hz
        assert(history.count_in_last(10000000, 1000000) >= 59);
        for (uint64_t t = 10025000; t <= 11000000; t += 25000)
            history.record(t); // 40 Hz, starts at the 10s boundary
        assert(history.count_in_last(10500000, 1000000) < 55);
        assert(history.count_in_last(11000000, 1000000) < 55);
    }

    // The 1s window is (now-1s, now]: 55 samples spanning 54 intervals
    // over exactly one second are 54Hz and must not satisfy the 55Hz gate.
    {
        PresentationHistory history;
        for (uint64_t i = 0; i <= 54; ++i)
            history.record(i * 1000000 / 54);
        assert(history.count_in_last(1000000, 1000000) == 54);
        for (uint64_t i = 1; i <= 55; ++i)
            history.record(1000000 + i * 1000000 / 55);
        assert(history.count_in_last(2000000, 1000000) == 55);
    }

    // T0: runtime's D3D ring classifier marks only Draw op ordinals as world.
    {
        Ring mixed;
        mixed.put(Op::Begin, {320, 240});
        mixed.put(Op::Viewport, {0, 0, 320, 240});
        emit_draw(mixed, 7, 10.f);
        mixed.put(Op::State, {RS_TEXTUREMIN, 2});
        emit_draw(mixed, 7, 20.f);
        mixed.put(Op::End, {});
        mixed.put(Op::Present, {});
        assert(d3d_world_draw_ordinals(mixed) == std::vector<uint32_t>({0, 1}));
        Ring no_draws;
        no_draws.put(Op::Begin, {320, 240});
        no_draws.put(Op::End, {});
        assert(d3d_world_draw_ordinals(no_draws).empty());
        Interp60 presenter;
        Ring a = two_draw_frame(10.f), b = two_draw_frame(30.f);
        assert(presenter.capture(a, 100000, d3d_world_draw_ordinals(a)));
        assert(presenter.capture(b, 125000, d3d_world_draw_ordinals(b)));
        Ring out;
        InterpStats stats;
        assert(presenter.tick(141667, out, &stats));
        assert(stats.interpolated == 2 && stats.fallback == 0);
    }

    // T1: snapshot deep-copy + first frame fallback is byte-equivalent.
    {
        Interp60 p;
        Ring source = frame(11.f);
        const std::vector<uint32_t> expected = source.words();
        assert(p.capture(source, 100000));
        source.clear();
        Ring out;
        InterpStats st;
        assert(p.tick(200000, out, &st));
        assert(out.words() == expected);
        assert(std::fabs(first_draw_x(out) - 11.f) < 0.001f);
        assert(st.history_ready == false && st.interpolated == 0 && st.fallback == 1);
    }

    // T2: two adjacent frames interpolate at 60Hz with one guest-frame latency.
    {
        Interp60 p;
        Ring a = frame(10.f), b = frame(30.f);
        assert(p.capture(a, 100000, {0}));
        assert(p.capture(b, 125000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(125000, out, &st));   // target=100ms, alpha=0
        assert(std::fabs(first_draw_x(out) - 10.f) < 0.001f);
        assert(p.tick(141667, out, &st));   // target=116667us, alpha≈2/3
        assert(std::fabs(first_draw_x(out) - 23.3333f) < 0.01f);
        assert(st.history_ready && st.interpolated == 1 && st.fallback == 0);
        assert(!p.tick(150000, out, &st));  // not yet at next 60Hz deadline
        assert(p.tick(158334, out, &st));   // target reaches current snapshot
        assert(std::fabs(first_draw_x(out) - 30.f) < 0.001f);
    }

    // Keep the previous double arithmetic's exact final float bits.
    {
        Interp60 p;
        assert(p.capture(wide_frame(621.2657470703125f), 100000, {0}));
        assert(p.capture(wide_frame(-119.51024627685547f), 125000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        bool golden = false;
        out.for_each([&](uint16_t op, const uint32_t* payload, uint32_t) {
            if (op == uint16_t(Op::Draw)) golden = payload[2] == 0x42fecf77u;
        });
        assert(golden && st.interpolated == 1);
    }

    // Texture generation churn is bounded and transactional on overflow.
    {
        Interp60 p;
        assert(p.capture(frame(10.f, 42), 100000, {0}));
        Ring overflow;
        overflow.put(Op::TexFree, {42});
        for (uint32_t id = 1000; id < 1000 + 384; ++id)
            overflow.put(Op::TexUpload, {id});
        overflow.put(Op::Begin, {320, 240});
        overflow.put(Op::Viewport, {0, 0, 320, 240});
        emit_draw(overflow, 42, 20.f);
        overflow.put(Op::End, {});
        overflow.put(Op::Present, {});
        assert(!p.capture(overflow, 125000, {0})); // 385 touched IDs exceed the 384 limit.
        assert(p.capture(frame(30.f, 42), 150000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(166667, out, &st));
        assert(st.interpolated == 1); // rejected frame did not mutate texture history.
    }

    {
        Interp60 p;
        for (uint32_t id = 0; id < 768; ++id) {
            Ring f = texture_event_frame(2000 + id, float(id));
            assert(p.capture(f, 100000 + uint64_t(id) * 25000, {0}));
        }
        Ring overflow = texture_event_frame(9000, 900.f);
        assert(!p.capture(overflow, 100000 + uint64_t(768) * 25000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(100000 + uint64_t(768) * 25000 + 16667, out, &st));
        assert(st.fallback == 1); // full-history overflow rejects cleanly, no alias.
    }

    // T3: changed topology or texture generation falls back for that draw.
    for (int mismatch = 0; mismatch < 3; ++mismatch) {
        Interp60 p;
        Ring a = frame(10.f, 7, 5, 4, true);
        Ring b = frame(40.f, mismatch == 0 ? 8 : 7,
                       mismatch == 1 ? 4 : 5,
                       mismatch == 2 ? 3 : 4,
                       mismatch == 2);
        assert(p.capture(a, 100000, {0}));
        assert(p.capture(b, 125000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(std::fabs(first_draw_x(out) - 40.f) < 0.001f);
        assert(st.interpolated == 0 && st.fallback == 1);
        assert(st.ordinal_texture_miss == (mismatch == 0 || mismatch == 2 ? 1u : 0u));
        assert(st.ordinal_topology_miss == (mismatch == 1 || mismatch == 2 ? 1u : 0u));
    }
    for (float invalid_x : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()}) {
        Interp60 p;
        assert(p.capture(frame(1.f), 100000));
        assert(!p.capture(frame(invalid_x), 125000));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(std::fabs(first_draw_x(out) - 1.f) < 0.001f);
        assert(!st.history_ready && st.interpolated == 0 && st.fallback == 1);
    }

    // T4: reject non-finite or implausibly large displacement, keep current draw.
    {
        Interp60 p;
        Ring a = frame(1.f), b = frame(400.f);
        assert(p.capture(a, 100000, {0}));
        assert(p.capture(b, 125000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(std::fabs(first_draw_x(out) - 400.f) < 0.001f);
        assert(st.interpolated == 0 && st.fallback == 1);
        assert(st.displacement_reject == 1);
    }

    // T5: the rational 60Hz pacer emits 60 ticks in [0, 1s), without catch-up bursts.
    {
        Interp60 p;
        Ring f = frame(0.f);
        assert(p.capture(f, 0));
        Ring out;
        uint32_t ticks = 0;
        for (uint64_t us = 0; us < 1000000; us += 250) ticks += p.tick(us, out) ? 1u : 0u;
        assert(ticks == 60);
    }

    // T6: the presenter replays each due snapshot through one persistent backend.
    {
        CountingBackend backend;
        Interp60Presenter presenter(backend);
        Ring a = frame(10.f), b = frame(30.f);
        assert(presenter.capture(a, 100000, {0}));
        assert(presenter.capture(b, 125000, {0}));
        assert(presenter.tick(125000));
        assert(presenter.tick(141667));
        assert(!presenter.tick(150000));
        assert(backend.presents == 2 && backend.begins == 2 && backend.ends == 2 && backend.draws == 2);
        assert(std::fabs(backend.last_x - Replayer::to_clip(23.3333f, 0.f, 0.f, 0.f, 320, 240).x) < 0.001f);
    }

    // T7: malformed and non-monotonic captures do not replace valid history.
    {
        Interp60 p;
        Ring a = frame(5.f), malformed;
        malformed.put(Op::Begin, {320, 240});
        assert(p.capture(a, 100000, {0}));
        assert(!p.capture(malformed, 110000));
        Ring same_time = frame(99.f);
        assert(!p.capture(same_time, 100000));
        Ring b = frame(25.f);
        assert(p.capture(b, 125000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(std::fabs(first_draw_x(out) - 18.3333f) < 0.01f);
        assert(st.interpolated == 1);
    }

    // Unclassified draws (including HUD) must never move.
    {
        Interp60 p;
        assert(p.capture(frame(10.f), 100000));
        assert(p.capture(frame(30.f), 125000));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(first_draw_x(out) == 30.f && st.interpolated == 0);
    }

    // The bounded spatial matcher preserves painter order; viewport changes
    // remain a hard identity boundary.
    {
        Interp60 p;
        assert(p.capture(two_draw_frame(10.f), 100000, {0, 1}));
        assert(p.capture(two_draw_frame(30.f), 125000, {0, 1}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        std::vector<float> xs;
        out.for_each([&](uint16_t op, const uint32_t* q, uint32_t n) {
            if (op == uint16_t(Op::Draw) && n >= 8) xs.push_back(as_float(q[2]));
        });
        assert(xs.size() == 2);
        assert(std::fabs(xs[0] - 23.3333f) < 0.01f);
        assert(std::fabs(xs[1] - 123.3333f) < 0.01f);
        assert(st.interpolated == 2 && st.fallback == 0);
    }
    {
        Interp60 p;
        assert(p.capture(two_draw_frame(10.f), 100000, {0, 1}));
        assert(p.capture(two_draw_frame(30.f, 1), 125000, {0, 1}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(st.interpolated == 0 && st.fallback == 2);
        assert(st.ordinal_viewport_miss == 2);
    }

    // HW-066: insert/delete shifts ordinals and total draw count, but stable
    // world draws still match one-to-one within the bounded order window.
    {
        Interp60 p;
        const std::vector<DrawItem> before = {
            {7, 10.f}, {8, 70.f}, {9, 140.f}, {10, 220.f}
        };
        const std::vector<DrawItem> after = {
            {99, 3.f}, {7, 30.f}, {8, 90.f}, {9, 160.f}, {10, 240.f}
        };
        assert(p.capture(pattern_frame(before), 100000, {0, 1, 2, 3}));
        assert(p.capture(pattern_frame(after), 125000, {0, 1, 2, 3, 4}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(st.interpolated == 4 && st.fallback == 1);
        assert(st.draw_count_delta == 1 && st.ordinal_texture_miss == 4);
        assert(st.rematched_order == 4 && st.no_candidate_window == 1);
        std::puts("HW-066 synthetic insertion match=4/5 (80%)");
        std::vector<float> xs;
        out.for_each([&](uint16_t op, const uint32_t* q, uint32_t n) {
            if (op == uint16_t(Op::Draw) && n >= 8) xs.push_back(as_float(q[2]));
        });
        assert(xs.size() == 5 && xs[0] == 3.f);
        assert(std::fabs(xs[1] - 23.3333f) < 0.01f);
        assert(std::fabs(xs[4] - 233.3333f) < 0.01f);
    }
    {
        Interp60 p;
        const std::vector<DrawItem> before = {
            {7, 10.f}, {8, 70.f}, {9, 140.f}, {10, 220.f}
        };
        const std::vector<DrawItem> after = {
            {7, 30.f}, {9, 160.f}, {10, 240.f}
        };
        assert(p.capture(pattern_frame(before), 100000, {0, 1, 2, 3}));
        assert(p.capture(pattern_frame(after), 125000, {0, 1, 2}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(st.interpolated == 3 && st.fallback == 0);
        assert(st.draw_count_delta == 1 && st.rematched_order == 2);
        std::puts("HW-066 synthetic deletion match=3/3 (100%)");
    }
    {
        Interp60 p;
        const auto before = pattern_frame({{7, 10.f}, {7, 200.f}});
        const auto after = pattern_frame({{7, 190.f}, {7, 20.f}});
        assert(p.capture(before, 100000, {0, 1}));
        assert(p.capture(after, 125000, {0, 1}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        std::vector<float> xs;
        out.for_each([&](uint16_t op, const uint32_t* q, uint32_t n) {
            if (op == uint16_t(Op::Draw) && n >= 8) xs.push_back(as_float(q[2]));
        });
        assert(xs.size() == 2 && std::fabs(xs[0] - 193.3333f) < 0.01f);
        assert(xs[1] == 20.f && st.interpolated == 1 && st.fallback == 1);
        assert(st.displacement_reject == 0);
    }
    {
        // An ordinal match within the viewport is not necessarily the spatial
        // minimum when identical textures repeat; don't take the hot path.
        Interp60 p;
        const auto before = pattern_frame({{7, 10.f}, {7, 50.f}});
        const auto after = pattern_frame({{7, 49.f}, {7, 11.f}});
        assert(p.capture(before, 100000, {0, 1}));
        assert(p.capture(after, 125000, {0, 1}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        std::vector<float> xs;
        out.for_each([&](uint16_t op, const uint32_t* q, uint32_t n) {
            if (op == uint16_t(Op::Draw) && n >= 8) xs.push_back(as_float(q[2]));
        });
        assert(xs.size() == 2 && std::fabs(xs[0] - 49.3333f) < 0.01f);
        assert(xs[1] == 11.f && st.interpolated == 1 && st.fallback == 1);
    }

    // An unclassified HUD-like draw is copied byte-for-byte; re-matching the
    // surrounding world draws never opts it into interpolation.
    {
        Interp60 p;
        const auto before = pattern_frame({{7, 10.f}, {99, 40.f}, {8, 100.f}});
        const auto after = pattern_frame({{99, 55.f}, {7, 30.f}, {8, 120.f}});
        assert(p.capture(before, 100000, {0, 2}));
        assert(p.capture(after, 125000, {1, 2}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        std::vector<float> xs;
        out.for_each([&](uint16_t op, const uint32_t* q, uint32_t n) {
            if (op == uint16_t(Op::Draw) && n >= 8) xs.push_back(as_float(q[2]));
        });
        assert(xs.size() == 3 && xs[0] == 55.f);
        assert(std::fabs(xs[1] - 23.3333f) < 0.01f);
        assert(st.interpolated == 2 && st.fallback == 1);
    }

    // HW-035: immutable Ring ownership and all-world fast path preserve the
    // compatibility route's exact output while avoiding its input copy/list.
    {
        Interp60 compatibility, zero_copy;
        auto a = std::make_shared<Ring>(two_draw_frame(10.f));
        auto b = std::make_shared<Ring>(two_draw_frame(30.f));
        const auto wa = d3d_world_draw_ordinals(*a);
        const auto wb = d3d_world_draw_ordinals(*b);
        assert(compatibility.capture(*a, 100000, wa));
        assert(compatibility.capture(*b, 125000, wb));
        assert(zero_copy.capture_world_frame(a, 100000));
        assert(zero_copy.capture_world_frame(b, 125000));
        Ring expected, actual;
        InterpStats expected_stats{}, actual_stats{};
        assert(compatibility.tick(141667, expected, &expected_stats));
        assert(zero_copy.tick(141667, actual, &actual_stats));
        assert(expected.words() == actual.words());
        assert(expected_stats.interpolated == actual_stats.interpolated);
        assert(expected_stats.fallback == actual_stats.fallback);
        assert(expected_stats.alpha == actual_stats.alpha);
    }

    // All primitive topologies supported by Device preserve correspondence;
    // only XY moves, while depth and texture coordinates stay intact.
    for (uint32_t prim : {4u, 5u, 6u}) {
        const uint32_t count = prim == 4 ? 3u : 4u;
        Interp60 p;
        assert(p.capture(device_frame(10.f, prim, count), 100000, {0}));
        assert(p.capture(device_frame(30.f, prim, count), 125000, {0}));
        Ring out;
        InterpStats st;
        assert(p.tick(141667, out, &st));
        assert(st.interpolated == 1 && st.fallback == 0);
        bool checked = false;
        out.for_each([&](uint16_t op, const uint32_t* q, uint32_t n) {
            if (op != uint16_t(Op::Draw)) return;
            assert(q[0] == prim && q[1] == count && n == 2 + count * 6);
            assert(std::fabs(as_float(q[2]) - 23.3333f) < 0.01f);
            assert(std::fabs(as_float(q[4]) - 0.25f) < 0.001f);
            assert(as_float(q[5]) == 1.f);
            assert(as_float(q[6]) == 0.f && as_float(q[7]) == 0.f);
            checked = true;
        });
        assert(checked);
    }

    std::puts("test_interp60 OK");
    return 0;
}
