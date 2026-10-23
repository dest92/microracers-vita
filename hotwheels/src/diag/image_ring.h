#pragma once

#include "d3d/d3d.h"

#include <map>

namespace hw::diag {

// Rewrites only the sampler state consumed by the existing Replayer. Resource,
// geometry and ordering commands remain byte-for-byte equivalent.
class ImageRingFilter {
public:
    explicit ImageRingFilter(hw::d3d::TextureFilter policy) : policy_(policy) {}

    bool rewrite(const hw::d3d::Ring& input, hw::d3d::Ring& output) {
        size_t cursor = 0;
        uint16_t raw_op = 0;
        const uint32_t* p = nullptr;
        uint32_t n = 0;
        while (cursor < input.words().size()) {
            if (!hw::d3d::Ring::next(input.words(), cursor, raw_op, p, n)) return false;
            const auto op = static_cast<hw::d3d::Op>(raw_op);
            if (op == hw::d3d::Op::State && n >= 2) {
                states_[p[0]] = p[1];
                output.put(op, p, n);
            } else if (op == hw::d3d::Op::Texture && n >= 2) {
                texture_ = p[1];
                output.put(op, p, n);
            } else if (op == hw::d3d::Op::TexUpload && n >= 6) {
                keyed_[p[0]] = (p[5] & 1u) != 0;
                output.put(op, p, n);
            } else if (op == hw::d3d::Op::TexFree && n >= 1) {
                keyed_.erase(p[0]);
                output.put(op, p, n);
            } else if (op == hw::d3d::Op::Draw) {
                const auto key = keyed_.find(texture_);
                const bool keyed_alpha = states_[winx86::dx::d3d::RS_COLORKEYENABLE] != 0 &&
                                         key != keyed_.end() && key->second;
                const bool original_linear = states_[hw::d3d::RS_TEXTUREMAG] == 2;
                const bool linear = effective_linear(policy_, original_linear, keyed_alpha);
                const uint32_t sampler = linear ? 2u : 1u;
                output.put(hw::d3d::Op::State, {hw::d3d::RS_TEXTUREMAG, sampler});
                output.put(op, p, n);
            } else {
                output.put(op, p, n);
            }
        }
        return true;
    }

private:
    hw::d3d::TextureFilter policy_;
    uint32_t texture_ = 0;
    std::map<uint32_t, uint32_t> states_;
    std::map<uint32_t, bool> keyed_;
};

} // namespace hw::diag
