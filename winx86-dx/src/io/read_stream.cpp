// Portable buffered stream implementation.
#include "../../include/winx86-dx/io/read_stream.h"
#include <algorithm>
#include <cstring>

namespace winx86::dx::io {

ReadStream::ReadStream(FILE* f, size_t window) : f_(f), win_cap_(window) {
    std::fseek(f_, 0, SEEK_END);
    size_ = (uint64_t)std::ftell(f_);
    std::fseek(f_, 0, SEEK_SET);
}

ReadStream::ReadStream(std::shared_ptr<const std::vector<uint8_t>> blob)
    : f_(nullptr), blob_(std::move(blob)), win_cap_(0) { size_ = blob_->size(); }

int64_t ReadStream::seek(int64_t dist, int whence) {
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return -1;
    const uint64_t raw_base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? pos_ : size_;
    if (raw_base > static_cast<uint64_t>(INT64_MAX)) return -1;
    const int64_t base = static_cast<int64_t>(raw_base);
    if (dist > 0 && base > INT64_MAX - dist) return -1;
    if (dist < 0 && dist < -base) return -1;
    const int64_t np = base + dist;
    pos_ = (uint64_t)np;
    return np;
}

size_t ReadStream::read(void* dst, size_t n) {
    auto* out = static_cast<uint8_t*>(dst);
    if (blob_) {
        if (pos_ >= size_) return 0;
        size_t k = (size_t)std::min<uint64_t>(n, size_ - pos_);
        if (k) std::memcpy(out, blob_->data() + pos_, k);
        pos_ += k;
        return k;
    }
    size_t done = 0;
    while (done < n && pos_ < size_) {
        if (pos_ >= win_off_ && pos_ < win_off_ + win_.size()) {
            size_t k = std::min<size_t>(n - done, win_off_ + win_.size() - pos_);
            std::memcpy(out + done, &win_[pos_ - win_off_], k);
            done += k; pos_ += k;
            continue;
        }
        size_t want = std::min<uint64_t>(n - done, size_ - pos_);
        if (want >= win_cap_) {  // lectura grande: directo, sin pasar por la ventana
            std::fseek(f_, (long)pos_, SEEK_SET);
            size_t g = std::fread(out + done, 1, want, f_);
            sys_ += 2;
            done += g; pos_ += g;
            if (g < want) break;
            continue;
        }
        win_.resize((size_t)std::min<uint64_t>(win_cap_, size_ - pos_));
        std::fseek(f_, (long)pos_, SEEK_SET);
        size_t g = std::fread(win_.data(), 1, win_.size(), f_);
        sys_ += 1;
        win_.resize(g); win_off_ = pos_;
        if (!g) break;
    }
    return done;
}

}  // namespace winx86::dx::io
