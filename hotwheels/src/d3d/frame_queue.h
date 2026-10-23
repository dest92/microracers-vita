#pragma once
#include "d3d/d3d.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace hw::d3d {
class FrameQueue {
public:
    struct Stats {
        uint64_t submitted=0, completed=0, dropped=0, close_waits=0;
        uint64_t dropped_resource_ops=0, carried_resource_ops=0;
        unsigned in_flight=0;
    };
    class Writer {
    public:
        Writer() = default;
        Writer(Writer&&) noexcept;
        Writer& operator=(Writer&&) noexcept;
        ~Writer();
        explicit operator bool() const { return ring_ != nullptr; }
        Ring* ring() const { return ring_; }
        // before_publish runs under the queue mutex; it must not re-enter FrameQueue.
        bool publish(const std::function<void()>& before_publish = {});
        void abandon();
    private:
        friend class FrameQueue;
        Writer(FrameQueue*, unsigned, Ring*);
        FrameQueue* owner_ = nullptr;
        unsigned slot_ = 0;
        Ring* ring_ = nullptr;
    };
    class Lease {
    public:
        Lease() = default;
        Lease(Lease&&) noexcept;
        Lease& operator=(Lease&&) noexcept;
        ~Lease();
        explicit operator bool() const { return ring_ != nullptr; }
        const Ring* ring() const { return ring_; }
        void finish();
    private:
        friend class FrameQueue;
        Lease(FrameQueue*, unsigned, const Ring*);
        FrameQueue* owner_ = nullptr;
        unsigned slot_ = 0;
        const Ring* ring_ = nullptr;
    };
    FrameQueue();
    ~FrameQueue();
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;
    Writer begin_write();
    Lease take();
    Lease wait_take();
    void close(bool drain);
    Stats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    void finish(unsigned);
    bool publish(unsigned, Ring*, const std::function<void()>& before_publish);
    void abandon(unsigned);
};
}
