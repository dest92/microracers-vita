#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace hw::integration::detail {

constexpr uint32_t kMmioTransferChunk = 4096;

// Copy a guest-requested MMIO read using fixed host scratch. `read` returns
// bytes read (0 at EOF, negative on error); `write` receives the output offset.
template<class Read, class Write>
int32_t mmio_read_bounded(int32_t count, Read&& read, Write&& write) {
    if (count <= 0) return count;
    std::array<uint8_t, kMmioTransferChunk> scratch{};
    int32_t total = 0;
    while (total < count) {
        const int32_t request = std::min<int32_t>(count - total,
                                                  int32_t(kMmioTransferChunk));
        const int32_t got = read(scratch.data(), request);
        if (got < 0) return total ? total : -1;
        if (got == 0) break;
        if (got > request) return total ? total : -1;
        if (!write(scratch.data(), uint32_t(got), uint32_t(total)))
            return total ? total : -1;
        total += got;
        if (got < request) break;
    }
    return total;
}

} // namespace hw::integration::detail
