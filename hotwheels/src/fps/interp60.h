// Guest-frame snapshots and deterministic 60Hz interpolation (HW-025).
#pragma once

#include "d3d/d3d.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace hw::fps {

// In this port the Vita D3D ring contains 3D scene geometry; HUD/menu blits
// travel through DirectDraw. Return Draw ordinals for the game-specific world
// classification used by HW-025. Unknown D3D commands are never classified.
std::vector<uint32_t> d3d_world_draw_ordinals(const d3d::Ring& frame);

// Fixed-capacity timestamp history for exact trailing presentation windows.
class PresentationHistory {
public:
    void record(uint64_t timestamp_us);
    uint32_t count_in_last(uint64_t now_us, uint64_t window_us) const;

private:
    static constexpr size_t kCapacity = 620; // > 60Hz * 10s, with boundary margin
    std::array<uint64_t, kCapacity> times_{};
    size_t next_ = 0, stored_ = 0;
};

struct InterpStats {
    bool history_ready = false;
    float alpha = 1.f;
    uint32_t draws = 0;
    uint32_t interpolated = 0;
    uint32_t fallback = 0;
    uint64_t skipped_presents = 0;
    uint32_t draw_count_delta = 0;
    uint32_t ordinal_texture_miss = 0;
    uint32_t ordinal_topology_miss = 0;
    uint32_t ordinal_viewport_miss = 0;
    uint32_t rematched_order = 0;
    uint32_t no_candidate_window = 0;
    uint32_t spatial_reject = 0;
    uint32_t displacement_reject = 0;
};

// Capture one complete Ring after Present. The reference overload copies the
// words for generic callers; production may transfer the immutable shared Ring
// owner through capture_world_frame. A separate serialized presenter consumes it.
class Interp60 {
public:
    Interp60();
    // world_draws lists zero-based ordinals positively identified as world geometry.
    // Empty means repeat all draws: unknown/UI must not be interpolated.
    // timestamp_us is monotonic host time. Incomplete, malformed, or
    // non-monotonic frames are rejected without replacing the last valid pair.
    bool capture(const d3d::Ring& completed_frame, uint64_t timestamp_us,
                 const std::vector<uint32_t>& world_draws = {});
    // Zero-copy production path: closed rings are immutable and owned by the
    // D3D present handoff. Every Draw is world geometry in this integration.
    bool capture_world_frame(std::shared_ptr<const d3d::Ring> completed_frame,
                             uint64_t timestamp_us);
    bool capture_world_frame(const d3d::Ring& completed_frame, uint64_t timestamp_us);

    // 60Hz clock gate. Returns true when the caller should replay out; missed
    // slots are skipped (never emitted as a catch-up burst). The caller owns
    // backend/GXM serialization and polls using its monotonic host clock.
    bool tick(uint64_t now_us, d3d::Ring& out, InterpStats* stats = nullptr);

    // Call only after producer and presenter have stopped.
    void reset();

    static constexpr uint32_t kHz = 60;

private:
    struct GenerationEntry {
        uint32_t texture = 0;
        uint64_t generation = 0;
        bool occupied = false;
    };
    struct GenerationTable {
        static constexpr size_t kBuckets = 1024;
        static constexpr size_t kMaxTextures = 768;
        std::array<GenerationEntry, kBuckets> slots{};
        size_t size = 0;
    };
    struct GenerationDelta {
        struct Entry { uint32_t texture = 0, count = 0, epoch = 0; };
        static constexpr size_t kBuckets = 512;
        static constexpr size_t kMaxTextures = 384;
        std::array<Entry, kBuckets> slots{};
        uint32_t epoch = 0;
        size_t size = 0;
    };
    struct Point { float x, y; };
    struct DrawMeta {
        bool world = false;
        uint32_t texture = 0, primitive = 0, vertex_count = 0;
        uint32_t point_offset = 0;
        uint64_t texture_generation = 0;
        int32_t viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;
        float center_x = 0.f, center_y = 0.f;
    };
    struct Snapshot {
        uint64_t frame_id = 0, timestamp_us = 0;
        uint32_t width = 0, height = 0;
        std::shared_ptr<const d3d::Ring> ring_owner;
        std::vector<uint32_t> owned_words;
        std::vector<DrawMeta> draws;
        std::vector<Point> points;
        const std::vector<uint32_t>& words() const {
            return ring_owner ? ring_owner->words() : owned_words;
        }
    };

    bool capture_impl(std::shared_ptr<const d3d::Ring> owner,
                      const std::vector<uint32_t>& words, uint64_t timestamp_us,
                      const std::vector<uint32_t>* world_draws, bool all_draws_world);
    static bool validate_and_decode(const std::vector<uint32_t>& words,
                                    GenerationTable& generations, GenerationDelta& delta,
                                    std::vector<DrawMeta>& draws, std::vector<Point>& points,
                                    bool all_draws_world,
                                    uint32_t& width, uint32_t& height);
    static bool commit_generations(GenerationTable& generations,
                                   const GenerationDelta& delta);
    static bool build_interpolated(const Snapshot* previous, const Snapshot& current,
                                   uint64_t present_us, d3d::Ring& out, InterpStats& stats);
    bool clock_due(uint64_t now_us, uint64_t& skipped);

    std::mutex mutex_;
    std::shared_ptr<const Snapshot> previous_, current_;
    std::array<std::shared_ptr<Snapshot>, 4> snapshot_pool_{};
    GenerationTable texture_generations_{};
    GenerationDelta generation_delta_{};
    uint64_t next_frame_id_ = 1;

    bool clock_started_ = false;
    uint64_t clock_epoch_us_ = 0;
    uint64_t next_tick_index_ = 0;
};

// Synchronous 60Hz presenter adapter. capture() runs after each guest Present;
// tick() is called by the chosen presentation loop and replays through one
// persistent Replayer/backend instance, so texture state survives ring copies.
class Interp60Presenter {
public:
    explicit Interp60Presenter(d3d::Backend& backend) : replayer_(backend) {}
    bool capture(const d3d::Ring& completed_frame, uint64_t timestamp_us,
                 const std::vector<uint32_t>& world_draws = {}) {
        return frames_.capture(completed_frame, timestamp_us, world_draws);
    }
    bool capture_world_frame(std::shared_ptr<const d3d::Ring> completed_frame,
                             uint64_t timestamp_us) {
        return frames_.capture_world_frame(std::move(completed_frame), timestamp_us);
    }
    bool capture_world_frame(const d3d::Ring& completed_frame, uint64_t timestamp_us) {
        return frames_.capture_world_frame(completed_frame, timestamp_us);
    }
    bool tick(uint64_t now_us, InterpStats* stats = nullptr) {
        if (!frames_.tick(now_us, frame_, stats)) return false;
        replayer_.play(frame_);
        return true;
    }
    void reset() { frames_.reset(); }

private:
    Interp60 frames_;
    d3d::Replayer replayer_;
    d3d::Ring frame_; // retained capacity across 60Hz presents
};

} // namespace hw::fps
