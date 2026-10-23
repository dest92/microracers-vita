// Compatibility wrapper for the Hot Wheels cache and generic ReadStream.
#pragma once
#include "../../../winx86-dx/include/winx86-dx/io/read_stream.h"
#include <map>
#include <string>

namespace hw::io {
using namespace ::winx86::dx::io;

// Game-local shared blob cache; single-threaded under the current GIL policy.
class BlobCache {
public:
    static BlobCache& instance();
    std::shared_ptr<const std::vector<uint8_t>> get(const std::string& path);
    void invalidate(const std::string& path);
    void configure(size_t max_file, size_t max_total);
    uint64_t hits = 0, misses = 0;
private:
    std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> m_;
    size_t max_file_ = 1u << 20, max_total_ = 16u << 20, total_ = 0;
};
}  // namespace hw::io
