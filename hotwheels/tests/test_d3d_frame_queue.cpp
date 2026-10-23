#include "d3d/frame_queue.h"
#include <chrono>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, sizeof u); return u; }

int main() {
    using namespace hw::d3d;
    FrameQueue q;
    unsigned producer_ticks = 0;
    auto write = q.begin_write();
    assert(write);
    write.ring()->put(Op::Clear, {1, 2, 3, 4, 5});
    assert(write.publish([&] { ++producer_ticks; }));
    assert(producer_ticks == 1);
    auto a = q.take();
    assert(a && a.ring()->words().size() == 6);
    assert(q.begin_write().ring() != a.ring());

    auto second = q.begin_write();
    assert(second);
    second.ring()->put(Op::Present, {});
    assert(second.publish());
    auto third = q.begin_write();
    assert(third);
    third.ring()->put(Op::Present, {});
    assert(third.publish());
    // El slot in-flight se conserva; ante presión cae el READY más viejo.
    auto stats = q.stats();
    assert(stats.submitted == 3 && stats.dropped == 1 && stats.in_flight == 1);
    assert(a.ring()->words().size() == 6);
    a.finish();
    auto b = q.take();
    assert(b && !q.take());
    assert(b.ring()->words().size() == 1); // A dropped non-resource ring adds no prelude.
    b.finish();
    assert(q.stats().completed == 2);

    auto last = q.begin_write();
    assert(last);
    last.abandon();
    q.close(false);
    assert(!q.begin_write());
    assert(!q.take());

    FrameQueue wake;
    auto started = std::promise<void>();
    auto waiting = std::async(std::launch::async, [&] {
        started.set_value();
        auto lease = wake.wait_take();
        assert(lease && lease.ring()->words().size() == 1);
    });
    started.get_future().wait();
    auto queued = wake.begin_write();
    queued.ring()->put(Op::Present, {});
    assert(queued.publish());
    waiting.get();
    wake.close(true);
    auto stopped = std::async(std::launch::async, [&] { return wake.wait_take(); });
    assert(!stopped.get());

    FrameQueue closing;
    auto active_writer = closing.begin_write();
    auto close_started = std::promise<void>();
    auto closer = std::async(std::launch::async, [&] {
        close_started.set_value();
        closing.close(false);
    });
    close_started.get_future().wait();
    while (closing.stats().close_waits == 0) std::this_thread::yield();
    assert(closer.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    unsigned rejected_ticks = 0;
    assert(!active_writer.publish([&] { ++rejected_ticks; }));
    closer.get();
    assert(rejected_ticks == 0);

    // A dropped frame must carry its final texture state into the next frame.
    // The discarded ring performs Upload(red) -> Free -> Upload(green); a later
    // surviving draw must rasterize green after the carry was replayed.
    FrameQueue resources;
    auto resource_writer = resources.begin_write();
    const uint32_t red[] = {9, 1, 1, uint32_t(TexFormat::ARGB1555), 2, 0, 0, 0, 0xfc00};
    const uint32_t green[] = {9, 1, 1, uint32_t(TexFormat::ARGB1555), 2, 0, 0, 0, 0x83e0};
    resource_writer.ring()->put(Op::TexUpload, red, 9);
    resource_writer.ring()->put(Op::TexFree, {9});
    resource_writer.ring()->put(Op::TexUpload, green, 9);
    resource_writer.ring()->put(Op::Texture, {0, 9});
    assert(resource_writer.publish());
    auto filler = resources.begin_write();
    filler.ring()->put(Op::Present, {});
    assert(filler.publish());
    auto draw_writer = resources.begin_write();
    draw_writer.ring()->put(Op::Begin, {4, 4});
    const uint32_t draw[] = {
        4, 3,
        bits(0), bits(0), bits(0), bits(1), bits(0), bits(0),
        bits(4), bits(0), bits(0), bits(1), bits(1), bits(0),
        bits(0), bits(4), bits(0), bits(1), bits(0), bits(1)
    };
    draw_writer.ring()->put(Op::Draw, draw, sizeof(draw) / sizeof(draw[0]));
    draw_writer.ring()->put(Op::End, {});
    assert(draw_writer.publish()); // Drops the texture transition frame.
    assert(resources.stats().dropped == 1);
    assert(resources.stats().dropped_resource_ops == 4);
    assert(resources.stats().carried_resource_ops == 0);
    RefRaster golden;
    Replayer replay(golden);
    auto carried = resources.take();
    assert(carried);
    unsigned carried_uploads = 0;
    carried.ring()->for_each([&](uint16_t op, const uint32_t*, uint32_t) {
        if (op == uint16_t(Op::TexUpload)) ++carried_uploads;
    });
    assert(carried_uploads == 1);
    assert(resources.stats().carried_resource_ops == 2);
    replay.play(*carried.ring());
    carried.finish();
    auto survivor = resources.take();
    assert(survivor);
    replay.play(*survivor.ring());
    survivor.finish();
    assert(golden.pixel(1, 1) == 0xff00ff00u);
    resources.close(false);

    FrameQueue close_drop;
    auto close_writer = close_drop.begin_write();
    close_writer.ring()->put(Op::TexUpload, red, 9);
    assert(close_writer.publish());
    close_drop.close(false);
    assert(close_drop.stats().dropped == 1);
    assert(close_drop.stats().dropped_resource_ops == 1);
    assert(close_drop.stats().carried_resource_ops == 0);
}
