// src/io/read_cache.cpp — implementación de la caché de lectura (HW-020).
#include "io/read_cache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace hw::io {

void ReadCache::configure(size_t max_file_bytes, size_t max_total_bytes) {
    max_file_ = max_file_bytes;
    max_total_ = max_total_bytes;
}

void ReadCache::clear() {
    blobs_.clear();
    stats_ = CacheStats{};
}

bool ReadCache::load(const std::string& host_path) {
    if (blobs_.count(host_path)) return true;
    std::error_code ec;
    if (!fs::is_regular_file(host_path, ec)) { ++stats_.bypasses; return false; }
    const uint64_t size = (uint64_t)fs::file_size(host_path, ec);
    if (ec) { ++stats_.bypasses; return false; }
    if (size > max_file_ || stats_.bytes + size > max_total_) { ++stats_.bypasses; return false; }

    std::ifstream in(host_path, std::ios::binary);
    if (!in) { ++stats_.bypasses; return false; }
    std::vector<uint8_t> buf((size_t)size);
    if (size && !in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)size)) {
        ++stats_.bypasses;
        return false;
    }
    stats_.bytes += size;
    ++stats_.files;
    blobs_.emplace(host_path, std::move(buf));
    return true;
}

const std::vector<uint8_t>* ReadCache::get(const std::string& host_path) const {
    auto it = blobs_.find(host_path);
    if (it == blobs_.end()) { ++stats_.misses; return nullptr; }
    ++stats_.hits;
    return &it->second;
}

size_t ReadCache::preload_dir(const std::string& host_dir, bool recursive) {
    const auto t0 = std::chrono::steady_clock::now();
    size_t loaded = 0;
    std::error_code ec;
    if (!fs::is_directory(host_dir, ec)) return 0;
    auto opts = fs::directory_options::skip_permission_denied;
    if (recursive) {
        for (auto it = fs::recursive_directory_iterator(host_dir, opts, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && load(it->path().string())) ++loaded;
        }
    } else {
        for (auto it = fs::directory_iterator(host_dir, opts, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && load(it->path().string())) ++loaded;
        }
    }
    stats_.preload_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    return loaded;
}

}  // namespace hw::io
