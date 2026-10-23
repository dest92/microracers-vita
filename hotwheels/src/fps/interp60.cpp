#include "fps/interp60.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace hw::fps {

std::vector<uint32_t> d3d_world_draw_ordinals(const d3d::Ring& frame) {
    std::vector<uint32_t> ordinals;
    frame.for_each([&](uint16_t op, const uint32_t*, uint32_t) {
        if (op == uint16_t(d3d::Op::Draw))
            ordinals.push_back(uint32_t(ordinals.size()));
    });
    return ordinals;
}

void PresentationHistory::record(uint64_t timestamp_us) {
    times_[next_] = timestamp_us;
    next_ = (next_ + 1) % kCapacity;
    if (stored_ < kCapacity) ++stored_;
}

uint32_t PresentationHistory::count_in_last(uint64_t now_us, uint64_t window_us) const {
    uint32_t count = 0;
    for (size_t age = 0; age < stored_; ++age) {
        const size_t index = (next_ + kCapacity - 1 - age) % kCapacity;
        const uint64_t timestamp = times_[index];
        // Use (now-window, now]: counting both endpoints adds one sample over
        // the interval count and can turn 54 intervals/s into a false 55 Hz.
        if (timestamp <= now_us && now_us - timestamp < window_us) ++count;
        else if (timestamp <= now_us) break; // history is monotonic
    }
    return count;
}

namespace {

uint32_t float_bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

float bits_float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

} // namespace

bool Interp60::validate_and_decode(const std::vector<uint32_t>& words,
                                   GenerationTable& generations, GenerationDelta& delta,
                                   std::vector<DrawMeta>& draws, std::vector<Point>& points,
                                   bool all_draws_world,
                                   uint32_t& width, uint32_t& height) {
    draws.clear();
    points.clear();
    bool saw_begin = false, saw_end = false, saw_present = false;
    uint32_t active_texture = 0;
    uint64_t active_texture_generation = 0;
    int32_t viewport_x = 0, viewport_y = 0;
    int32_t viewport_w = 0, viewport_h = 0;
    auto hash_id = [](uint32_t id) {
        id ^= id >> 16; id *= 0x7feb352du;
        id ^= id >> 15; id *= 0x846ca68bu;
        return id ^ (id >> 16);
    };
    auto find_generation = [&](uint32_t id) -> GenerationEntry* {
        size_t slot = hash_id(id) & (GenerationTable::kBuckets - 1);
        for (size_t n = 0; n < GenerationTable::kBuckets; ++n) {
            auto& entry = generations.slots[slot];
            if (!entry.occupied) return nullptr;
            if (entry.texture == id) return &entry;
            slot = (slot + 1) & (GenerationTable::kBuckets - 1);
        }
        return nullptr;
    };
    auto find_delta = [&](uint32_t id, bool create) -> GenerationDelta::Entry* {
        size_t slot = hash_id(id) & (GenerationDelta::kBuckets - 1);
        for (size_t n = 0; n < GenerationDelta::kBuckets; ++n) {
            auto& entry = delta.slots[slot];
            if (entry.epoch != delta.epoch) {
                if (!create || delta.size >= GenerationDelta::kMaxTextures) return nullptr;
                entry = {id, 0, delta.epoch};
                ++delta.size;
                return &entry;
            }
            if (entry.texture == id) return &entry;
            slot = (slot + 1) & (GenerationDelta::kBuckets - 1);
        }
        return nullptr;
    };
    auto generation_for = [&](uint32_t id) {
        const auto* base = find_generation(id);
        const auto* changes = find_delta(id, false);
        return (base ? base->generation : 0) + (changes ? changes->count : 0);
    };

    if (++delta.epoch == 0) {
        for (auto& entry : delta.slots) entry.epoch = 0;
        delta.epoch = 1;
    }
    delta.size = 0;

    for (size_t i = 0; i < words.size();) {
        uint16_t op;
        uint32_t n;
        const uint32_t* p;
        size_t next = i;
        if (!d3d::Ring::next(words, next, op, p, n)) return false;
        if (saw_present) return false; // Present closes the copied frame.

        switch (d3d::Op(op)) {
        case d3d::Op::Begin:
            if (n < 2 || saw_begin || p[0] == 0 || p[1] == 0) return false;
            width = p[0];
            height = p[1];
            viewport_w = int32_t(width);
            viewport_h = int32_t(height);
            saw_begin = true;
            break;
        case d3d::Op::Viewport:
            if (n < 4) return false;
            viewport_x = int32_t(p[0]);
            viewport_y = int32_t(p[1]);
            viewport_w = int32_t(p[2]);
            viewport_h = int32_t(p[3]);
            break;
        case d3d::Op::Texture:
            if (n < 2) return false;
            active_texture = p[1];
            active_texture_generation = generation_for(active_texture);
            break;
        case d3d::Op::TexUpload:
            if (n < 1) return false;
            if (auto* changes = find_delta(p[0], true)) ++changes->count;
            else return false;
            if (p[0] == active_texture) ++active_texture_generation;
            break;
        case d3d::Op::TexFree:
            if (n < 1) return false;
            if (auto* changes = find_delta(p[0], true)) ++changes->count;
            else return false;
            if (p[0] == active_texture) ++active_texture_generation;
            break;
        case d3d::Op::Draw: {
            if (!saw_begin || saw_end || n < 2) return false;
            const uint32_t primitive = p[0], count = p[1];
            if (count < 3 || size_t(count) > (std::numeric_limits<size_t>::max() - 2) / 6 ||
                size_t(n) < 2 + size_t(count) * 6) return false;
            DrawMeta d;
            d.texture = active_texture;
            d.texture_generation = active_texture_generation;
            d.primitive = primitive;
            d.vertex_count = count;
            d.world = all_draws_world;
            d.viewport_x = viewport_x;
            d.viewport_y = viewport_y;
            d.viewport_w = viewport_w;
            d.viewport_h = viewport_h;
            if (points.size() > std::numeric_limits<uint32_t>::max() - count) return false;
            d.point_offset = uint32_t(points.size());
            double center_x = 0.0, center_y = 0.0;
            for (uint32_t v = 0; v < count; ++v) {
                const size_t xoff = 2 + size_t(v) * 6;
                const float x = bits_float(p[xoff]);
                const float y = bits_float(p[xoff + 1]);
                if (!std::isfinite(x) || !std::isfinite(y)) return false;
                points.push_back({x, y});
                center_x += x;
                center_y += y;
            }
            d.center_x = float(center_x / count);
            d.center_y = float(center_y / count);
            draws.push_back(std::move(d));
            break;
        }
        case d3d::Op::End:
            if (!saw_begin || saw_end) return false;
            saw_end = true;
            break;
        case d3d::Op::Present:
            if (n != 0 || !saw_begin || !saw_end || next != words.size()) return false;
            saw_present = true;
            break;
        default:
            break; // Preserve unknown/future commands; the Replayer may ignore them.
        }
        i = next;
    }
    if (!saw_begin || !saw_end || !saw_present) return false;

    return true;
}

bool Interp60::commit_generations(GenerationTable& generations,
                                  const GenerationDelta& delta) {
    auto hash_id = [](uint32_t id) {
        id ^= id >> 16; id *= 0x7feb352du;
        id ^= id >> 15; id *= 0x846ca68bu;
        return id ^ (id >> 16);
    };
    auto find_generation = [&](uint32_t id) -> GenerationEntry* {
        size_t slot = hash_id(id) & (GenerationTable::kBuckets - 1);
        for (size_t n = 0; n < GenerationTable::kBuckets; ++n) {
            auto& entry = generations.slots[slot];
            if (!entry.occupied) return nullptr;
            if (entry.texture == id) return &entry;
            slot = (slot + 1) & (GenerationTable::kBuckets - 1);
        }
        return nullptr;
    };
    // Preflight capacity so overflow never commits partial generation history.
    size_t new_textures = 0;
    for (const auto& changes : delta.slots) {
        if (changes.epoch == delta.epoch && !find_generation(changes.texture)) ++new_textures;
    }
    if (new_textures > GenerationTable::kMaxTextures - generations.size) return false;
    for (const auto& changes : delta.slots) {
        if (changes.epoch != delta.epoch) continue;
        auto* entry = find_generation(changes.texture);
        if (!entry) {
            size_t slot = hash_id(changes.texture) & (GenerationTable::kBuckets - 1);
            while (generations.slots[slot].occupied)
                slot = (slot + 1) & (GenerationTable::kBuckets - 1);
            entry = &generations.slots[slot];
            *entry = {changes.texture, 0, true};
            ++generations.size;
        }
        entry->generation += changes.count;
    }
    return true;
}

bool Interp60::capture(const d3d::Ring& completed_frame, uint64_t timestamp_us,
                       const std::vector<uint32_t>& world_draws) {
    return capture_impl({}, completed_frame.words(), timestamp_us, &world_draws, false);
}

bool Interp60::capture_world_frame(std::shared_ptr<const d3d::Ring> completed_frame,
                                   uint64_t timestamp_us) {
    if (!completed_frame) return false;
    const auto& words = completed_frame->words();
    return capture_impl(std::move(completed_frame), words, timestamp_us, nullptr, true);
}
bool Interp60::capture_world_frame(const d3d::Ring& completed_frame, uint64_t timestamp_us) {
    return capture_impl({}, completed_frame.words(), timestamp_us, nullptr, true);
}

Interp60::Interp60() {
    for (auto& snapshot : snapshot_pool_) {
        snapshot = std::make_shared<Snapshot>();
        snapshot->draws.reserve(4096); // Carrera observada: p99 < 1.640; headroom para estrés.
        snapshot->points.reserve(16384); // 4 vértices por draw, 4x la escena observada.
    }
}

bool Interp60::capture_impl(std::shared_ptr<const d3d::Ring> owner,
                            const std::vector<uint32_t>& words, uint64_t timestamp_us,
                            const std::vector<uint32_t>* world_draws, bool all_draws_world) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (current_ && timestamp_us <= current_->timestamp_us) return false;

    std::shared_ptr<Snapshot> snapshot;
    for (auto& candidate : snapshot_pool_) {
        if (candidate.use_count() == 1) { snapshot = candidate; break; }
    }
    if (!snapshot) return false; // Todos los slots siguen visibles a un reader.

    snapshot->ring_owner.reset();
    snapshot->owned_words.clear();
    snapshot->draws.clear();
    snapshot->points.clear();
    uint32_t width = 0, height = 0;
    if (!validate_and_decode(words, texture_generations_, generation_delta_,
                             snapshot->draws, snapshot->points,
                             all_draws_world, width, height)) return false;

    if (world_draws) {
        for (uint32_t ordinal : *world_draws) {
            if (ordinal >= snapshot->draws.size()) return false;
            snapshot->draws[ordinal].world = true;
        }
    }
    if (!commit_generations(texture_generations_, generation_delta_)) return false;

    snapshot->frame_id = next_frame_id_++;
    snapshot->timestamp_us = timestamp_us;
    snapshot->width = width;
    snapshot->height = height;
    if (owner) snapshot->ring_owner = std::move(owner);
    else snapshot->owned_words.assign(words.begin(), words.end());
    previous_ = std::move(current_);
    current_ = std::move(snapshot);
    return true;
}

bool Interp60::clock_due(uint64_t now_us, uint64_t& skipped) {
    skipped = 0;
    if (!clock_started_) {
        clock_started_ = true;
        clock_epoch_us_ = now_us;
        next_tick_index_ = 1;
        return true; // First present can show the latest complete snapshot immediately.
    }
    if (now_us < clock_epoch_us_) return false;
    const uint64_t deadline = clock_epoch_us_ +
        (next_tick_index_ * 1000000ull + (kHz - 1)) / kHz;
    if (now_us < deadline) return false;
    const uint64_t elapsed_index = (now_us - clock_epoch_us_) * kHz / 1000000ull;
    if (elapsed_index > next_tick_index_) skipped = elapsed_index - next_tick_index_;
    next_tick_index_ = elapsed_index + 1;
    return true;
}

bool Interp60::build_interpolated(const Snapshot* previous, const Snapshot& current,
                                  uint64_t present_us, d3d::Ring& out, InterpStats& stats) {
    stats.history_ready = previous != nullptr;
    stats.draws = uint32_t(current.draws.size());
    stats.alpha = 1.f;

    double alpha = 1.0;
    if (previous && current.timestamp_us > previous->timestamp_us &&
        current.frame_id == previous->frame_id + 1) {
        const uint64_t interval = current.timestamp_us - previous->timestamp_us;
        // Render one guest interval behind the newest frame. This gives a
        // bounded interpolation interval instead of predicting past collisions.
        const uint64_t target_us = present_us > interval ? present_us - interval : 0;
        const double raw = (double(target_us) - double(previous->timestamp_us)) / double(interval);
        alpha = std::max(0.0, std::min(1.0, raw));
    } else {
        stats.history_ready = false;
    }
    stats.alpha = float(alpha);
    const bool frame_compatible = previous && alpha < 1.0 &&
        current.frame_id == previous->frame_id + 1 &&
        previous->width == current.width && previous->height == current.height;
    if (frame_compatible) {
        const size_t a = previous->draws.size(), b = current.draws.size();
        stats.draw_count_delta = uint32_t(a > b ? a - b : b - a);
    }

    out.clear();
    const auto& current_words = current.words();
    uint32_t ordinal = 0;
    size_t last_previous = 0;
    bool have_last_previous = false;
    for (size_t i = 0; i < current_words.size();) {
        uint16_t op;
        uint32_t n;
        const uint32_t* payload;
        size_t next = i;
        if (!d3d::Ring::next(current_words, next, op, payload, n)) return false;
        const DrawMeta* candidate_before = nullptr;
        const DrawMeta* candidate_now = nullptr;
        size_t candidate_index = 0;
        bool matched = false;
        if (d3d::Op(op) == d3d::Op::Draw) {
            if (ordinal >= current.draws.size()) return false;
            const DrawMeta& now = current.draws[ordinal];
            if (frame_compatible && now.world) {
                if (ordinal < previous->draws.size()) {
                    const DrawMeta& at_ordinal = previous->draws[ordinal];
                    if (at_ordinal.texture != now.texture ||
                        at_ordinal.texture_generation != now.texture_generation)
                        ++stats.ordinal_texture_miss;
                    if (at_ordinal.primitive != now.primitive ||
                        at_ordinal.vertex_count != now.vertex_count)
                        ++stats.ordinal_topology_miss;
                    if (at_ordinal.viewport_x != now.viewport_x ||
                        at_ordinal.viewport_y != now.viewport_y ||
                        at_ordinal.viewport_w != now.viewport_w ||
                        at_ordinal.viewport_h != now.viewport_h)
                        ++stats.ordinal_viewport_miss;
                }

                constexpr size_t kSearchRadius = 8;
                const size_t low = ordinal > kSearchRadius ? ordinal - kSearchRadius : 0;
                const size_t high = ordinal >= previous->draws.size()
                    ? previous->draws.size()
                    : ordinal + std::min(kSearchRadius + 1, previous->draws.size() - ordinal);
                const double max_x = double(current.width), max_y = double(current.height);
                const double max_center_distance2 = max_x * max_x + max_y * max_y;
                double best_distance2 = std::numeric_limits<double>::infinity();
                size_t best_ordinal_distance = std::numeric_limits<size_t>::max();
                bool fast_match = false, saw_signature = false, saw_spatial = false, order_blocked = false;
                auto same_signature = [&](const DrawMeta& before) {
                    return before.world &&
                        before.viewport_x == now.viewport_x && before.viewport_y == now.viewport_y &&
                        before.viewport_w == now.viewport_w && before.viewport_h == now.viewport_h &&
                        before.texture == now.texture &&
                        before.texture_generation == now.texture_generation &&
                        before.primitive == now.primitive && before.vertex_count == now.vertex_count;
                };
                // A zero-distance ordinal match is provably the nearest
                // candidate (and wins the ordinal-distance tie); otherwise
                // inspect the bounded neighborhood for the actual minimum.
                if (ordinal >= low && ordinal < high && ordinal < previous->draws.size() &&
                    (!have_last_previous || ordinal > last_previous) &&
                    same_signature(previous->draws[ordinal])) {
                    const DrawMeta& before = previous->draws[ordinal];
                    const double dx = double(before.center_x) - double(now.center_x);
                    const double dy = double(before.center_y) - double(now.center_y);
                    const double distance2 = dx * dx + dy * dy;
                    if (distance2 == 0.0 && distance2 <= max_center_distance2) {
                        candidate_index = ordinal;
                        best_distance2 = distance2;
                        best_ordinal_distance = 0;
                        fast_match = true;
                    }
                }
                for (size_t j = low; !fast_match && j < high; ++j) {
                    const DrawMeta& before = previous->draws[j];
                    if (!same_signature(before)) continue;
                    saw_signature = true;
                    if (have_last_previous && j <= last_previous) {
                        order_blocked = true;
                        continue;
                    }
                    const double dx = double(before.center_x) - double(now.center_x);
                    const double dy = double(before.center_y) - double(now.center_y);
                    const double distance2 = dx * dx + dy * dy;
                    if (distance2 > max_center_distance2) continue;
                    saw_spatial = true;
                    const size_t ordinal_distance = j > ordinal ? j - ordinal : ordinal - j;
                    if (distance2 < best_distance2 ||
                        (distance2 == best_distance2 && ordinal_distance < best_ordinal_distance)) {
                        best_distance2 = distance2;
                        best_ordinal_distance = ordinal_distance;
                        candidate_index = j;
                    }
                }
                if (best_distance2 != std::numeric_limits<double>::infinity()) {
                    candidate_before = &previous->draws[candidate_index];
                    candidate_now = &now;
                    if (candidate_index != ordinal) ++stats.rematched_order;
                    last_previous = candidate_index;
                    have_last_previous = true;
                } else if (saw_signature && !saw_spatial) {
                    ++stats.spatial_reject;
                } else if (order_blocked) {
                    ++stats.no_candidate_window;
                } else if (!saw_signature) {
                    ++stats.no_candidate_window;
                }
            }
            ++ordinal;
        }
        uint32_t* appended = out.put_patchable(d3d::Op(op), payload, n);
        if (candidate_before && candidate_now) {
            bool safe = true;
            const float max_dx = float(current.width), max_dy = float(current.height);
            for (size_t v = 0; v < candidate_now->vertex_count; ++v) {
                const Point& old_point = previous->points[candidate_before->point_offset + v];
                const Point& new_point = current.points[candidate_now->point_offset + v];
                const double dx = double(new_point.x) - double(old_point.x);
                const double dy = double(new_point.y) - double(old_point.y);
                if (!std::isfinite(dx) || !std::isfinite(dy) ||
                    std::fabs(dx) > max_dx || std::fabs(dy) > max_dy) {
                    // A late unsafe vertex falls back to the exact current draw.
                    for (size_t restore = 0; restore < v; ++restore) {
                        const size_t xy = 2 + restore * 6;
                        appended[xy] = payload[xy];
                        appended[xy + 1] = payload[xy + 1];
                    }
                    safe = false;
                    ++stats.displacement_reject;
                    break;
                }
                const size_t xy = 2 + v * 6;
                // Preserve the pre-optimization double arithmetic and final
                // float rounding bit-for-bit while still patching in place.
                const double x = double(old_point.x) + dx * alpha;
                const double y = double(old_point.y) + dy * alpha;
                appended[xy] = float_bits(float(x));
                appended[xy + 1] = float_bits(float(y));
            }
            matched = safe;
            if (matched) ++stats.interpolated;
        }
        i = next;
    }
    stats.fallback = stats.draws - stats.interpolated;
    return true;
}

bool Interp60::tick(uint64_t now_us, d3d::Ring& out, InterpStats* result) {
    uint64_t skipped = 0;
    if (!clock_due(now_us, skipped)) return false;

    std::shared_ptr<const Snapshot> previous, current;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        previous = previous_;
        current = current_;
    }
    if (!current) return false;

    InterpStats stats;
    stats.skipped_presents = skipped;
    if (!build_interpolated(previous.get(), *current, now_us, out, stats)) return false;
    if (result) *result = stats;
    return true;
}

void Interp60::reset() {
    std::lock_guard<std::mutex> guard(mutex_);
    previous_.reset();
    current_.reset();
    texture_generations_ = {};
    next_frame_id_ = 1;
    clock_started_ = false;
    clock_epoch_us_ = 0;
    next_tick_index_ = 0;
}

} // namespace hw::fps
