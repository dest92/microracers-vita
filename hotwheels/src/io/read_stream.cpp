// Hot Wheels BlobCache adapter plus generic ReadStream compatibility include.
#include "io/read_stream.h"
#include "../../../winx86-dx/src/io/read_stream.cpp"
#include <cstdio>
#include <memory>
#include <vector>
namespace hw::io {
BlobCache& BlobCache::instance() { static BlobCache c; return c; }
void BlobCache::configure(size_t mf, size_t mt) { max_file_ = mf; max_total_ = mt; }
void BlobCache::invalidate(const std::string& p) {
    auto it = m_.find(p);
    if (it == m_.end()) return;
    total_ -= it->second->size();
    m_.erase(it);
}
std::shared_ptr<const std::vector<uint8_t>> BlobCache::get(const std::string& p) {
    auto it = m_.find(p);
    if (it != m_.end()) { ++hits; return it->second; }
    ++misses;
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > max_file_ || total_ + (size_t)n > max_total_) { std::fclose(f); return nullptr; }
    auto b = std::make_shared<std::vector<uint8_t>>((size_t)n);
    size_t g = n ? std::fread(b->data(), 1, b->size(), f) : 0;
    std::fclose(f);
    if (g != (size_t)n) return nullptr;
    total_ += b->size();
    m_.emplace(p, b);
    return b;
}

}  // namespace hw::io
