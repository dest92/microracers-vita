#include "d3d/frame_queue.h"
#include <array>
#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>

namespace hw::d3d {
struct FrameQueue::Impl {
    enum class State : uint8_t { Free, Writing, Ready, InFlight };
    struct Slot { Ring ring; State state = State::Free; uint64_t sequence = 0; };
    struct ResourceOp { uint64_t sequence; Op op; std::vector<uint32_t> payload; };
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::array<Slot, 3> slots;
    int writer = 0;
    bool closed = false;
    bool writer_claimed = false;
    Stats counters;
    std::map<uint64_t, ResourceOp> resource_carry;
    uint64_t resource_sequence = 0;

    void remember_resources(const Ring& ring, bool carry = true) {
        ring.for_each([&](uint16_t raw, const uint32_t* payload, uint32_t n) {
            const Op op = Op(raw);
            if (((op != Op::TexUpload && op != Op::TexFree && op != Op::Texture)) || n < 1)
                return;
            ++counters.dropped_resource_ops;
            if (!carry) return;
            uint64_t key = payload[0];
            if (op == Op::Texture) key |= uint64_t(1) << 32;
            ResourceOp saved;
            saved.sequence = ++resource_sequence;
            saved.op = op;
            saved.payload.assign(payload, payload + n);
            resource_carry[key] = std::move(saved);
        });
    }

    void restore_resources(Ring& ring) {
        if (resource_carry.empty()) return;
        std::vector<const ResourceOp*> ordered;
        ordered.reserve(resource_carry.size());
        for (const auto& item : resource_carry) ordered.push_back(&item.second);
        counters.carried_resource_ops += ordered.size();
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
            return a->sequence < b->sequence;
        });
        Ring prefix;
        for (const auto* op : ordered)
            prefix.put(op->op, op->payload.data(), uint32_t(op->payload.size()));
        ring.prepend(prefix);
        resource_carry.clear();
    }

    Impl() { slots[0].state = State::Writing; }
};

FrameQueue::Writer::Writer(FrameQueue* owner, unsigned slot, Ring* ring)
    : owner_(owner), slot_(slot), ring_(ring) {}
FrameQueue::Writer::Writer(Writer&& o) noexcept
    : owner_(o.owner_), slot_(o.slot_), ring_(o.ring_) {
    o.owner_ = nullptr; o.ring_ = nullptr;
}
FrameQueue::Writer& FrameQueue::Writer::operator=(Writer&& o) noexcept {
    if (this != &o) {
        abandon(); owner_ = o.owner_; slot_ = o.slot_; ring_ = o.ring_;
        o.owner_ = nullptr; o.ring_ = nullptr;
    }
    return *this;
}
FrameQueue::Writer::~Writer() { abandon(); }
bool FrameQueue::Writer::publish(const std::function<void()>& before_publish) {
    if (!owner_) return false;
    const bool ok = owner_->publish(slot_, ring_, before_publish);
    owner_ = nullptr; ring_ = nullptr;
    return ok;
}
void FrameQueue::Writer::abandon() {
    if (owner_) owner_->abandon(slot_);
    owner_ = nullptr; ring_ = nullptr;
}
FrameQueue::Lease::Lease(FrameQueue* owner, unsigned slot, const Ring* ring)
    : owner_(owner), slot_(slot), ring_(ring) {}
FrameQueue::Lease::Lease(Lease&& o) noexcept
    : owner_(o.owner_), slot_(o.slot_), ring_(o.ring_) {
    o.owner_ = nullptr; o.ring_ = nullptr;
}
FrameQueue::Lease& FrameQueue::Lease::operator=(Lease&& o) noexcept {
    if (this != &o) {
        finish(); owner_ = o.owner_; slot_ = o.slot_; ring_ = o.ring_;
        o.owner_ = nullptr; o.ring_ = nullptr;
    }
    return *this;
}
FrameQueue::Lease::~Lease() { finish(); }
void FrameQueue::Lease::finish() {
    if (owner_) owner_->finish(slot_);
    owner_ = nullptr; ring_ = nullptr;
}
FrameQueue::FrameQueue() : p_(new Impl) {}
FrameQueue::~FrameQueue() { close(false); }
FrameQueue::Writer FrameQueue::begin_write() {
    std::lock_guard<std::mutex> lock(p_->mutex);
    if (p_->closed || p_->writer < 0 || p_->writer_claimed) return {};
    p_->writer_claimed = true;
    return Writer(this, unsigned(p_->writer), &p_->slots[unsigned(p_->writer)].ring);
}
bool FrameQueue::publish(unsigned index, Ring* ring,
                         const std::function<void()>& before_publish) {
    std::lock_guard<std::mutex> lock(p_->mutex);
    if (!p_->writer_claimed || p_->writer != int(index) ||
        ring != &p_->slots[index].ring) return false;
    auto& slot = p_->slots[index];
    p_->writer_claimed = false;
    if (p_->closed) {
        slot.ring.clear(); slot.state = Impl::State::Free; p_->writer = -1;
        p_->ready.notify_all();
        return false;
    }
    if (before_publish) before_publish();
    slot.state = Impl::State::Ready;
    slot.sequence = ++p_->counters.submitted;
    p_->writer = -1;
    for (unsigned i = 0; i < p_->slots.size(); ++i) {
        if (p_->slots[i].state == Impl::State::Free) {
            p_->writer = int(i);
            p_->slots[i].state = Impl::State::Writing;
            break;
        }
    }
    if (p_->writer < 0) {
        int oldest = -1;
        for (unsigned i = 0; i < p_->slots.size(); ++i)
            if (p_->slots[i].state == Impl::State::Ready &&
                (oldest < 0 || p_->slots[i].sequence < p_->slots[unsigned(oldest)].sequence))
                oldest = int(i);
        if (oldest >= 0) {
            auto& dropped = p_->slots[unsigned(oldest)];
            p_->remember_resources(dropped.ring);
            dropped.ring.clear(); dropped.state = Impl::State::Writing;
            p_->writer = oldest; ++p_->counters.dropped;
        }
    }
    p_->ready.notify_one();
    return true;
}
void FrameQueue::abandon(unsigned index) {
    std::lock_guard<std::mutex> lock(p_->mutex);
    if (!p_->writer_claimed || p_->writer != int(index)) return;
    p_->writer_claimed = false;
    p_->slots[index].ring.clear();
    if (p_->closed) { p_->slots[index].state = Impl::State::Free; p_->writer = -1; }
    p_->ready.notify_all();
}
FrameQueue::Lease FrameQueue::take() {
    std::lock_guard<std::mutex> lock(p_->mutex);
    int oldest = -1;
    for (unsigned i = 0; i < p_->slots.size(); ++i)
        if (p_->slots[i].state == Impl::State::Ready &&
            (oldest < 0 || p_->slots[i].sequence < p_->slots[unsigned(oldest)].sequence))
            oldest = int(i);
    if (oldest < 0) return {};
    auto& slot = p_->slots[unsigned(oldest)];
    p_->restore_resources(slot.ring);
    slot.state = Impl::State::InFlight;
    ++p_->counters.in_flight;
    return Lease(this, unsigned(oldest), &slot.ring);
}
FrameQueue::Lease FrameQueue::wait_take() {
    std::unique_lock<std::mutex> lock(p_->mutex);
    p_->ready.wait(lock, [&] {
        if (p_->closed) {
            for (const auto& slot : p_->slots)
                if (slot.state == Impl::State::Ready) return true;
            return true;
        }
        for (const auto& slot : p_->slots)
            if (slot.state == Impl::State::Ready) return true;
        return false;
    });
    int oldest = -1;
    for (unsigned i = 0; i < p_->slots.size(); ++i)
        if (p_->slots[i].state == Impl::State::Ready &&
            (oldest < 0 || p_->slots[i].sequence < p_->slots[unsigned(oldest)].sequence))
            oldest = int(i);
    if (oldest < 0) return {};
    auto& slot = p_->slots[unsigned(oldest)];
    p_->restore_resources(slot.ring);
    slot.state = Impl::State::InFlight;
    ++p_->counters.in_flight;
    return Lease(this, unsigned(oldest), &slot.ring);
}
void FrameQueue::close(bool drain) {
    std::unique_lock<std::mutex> lock(p_->mutex);
    if (p_->closed) return;
    p_->closed = true;
    p_->ready.notify_all();
    if (p_->writer_claimed) ++p_->counters.close_waits;
    p_->ready.wait(lock, [&] { return !p_->writer_claimed; });
    p_->writer = -1;
    for (auto& slot : p_->slots) {
        if (slot.state == Impl::State::Writing) slot.state = Impl::State::Free;
        if (!drain && slot.state == Impl::State::Ready) {
            p_->remember_resources(slot.ring, false);
            slot.ring.clear(); slot.state = Impl::State::Free; ++p_->counters.dropped;
        }
    }
    p_->ready.notify_all();
}
FrameQueue::Stats FrameQueue::stats() const {
    std::lock_guard<std::mutex> lock(p_->mutex);
    auto out = p_->counters;
    return out;
}
void FrameQueue::finish(unsigned i) {
    std::lock_guard<std::mutex> lock(p_->mutex);
    auto& slot = p_->slots[i];
    if (slot.state != Impl::State::InFlight) return;
    slot.ring.clear(); slot.state = Impl::State::Free;
    --p_->counters.in_flight; ++p_->counters.completed;
    if (!p_->closed && p_->writer < 0) {
        slot.state = Impl::State::Writing; p_->writer = int(i);
    }
    p_->ready.notify_all();
}
}
