// src/io/log_buffer.cpp — ver log_buffer.h.
#include "io/log_buffer.h"
#include <cctype>

namespace hw::io {

bool LogBuffer::critical(const std::string& s) {
    static const char* const k[] = {"FATAL", "fault", "FALLO", "error", "ERROR", "exit", "crash", "abort"};
    for (const char* w : k) if (s.find(w) != std::string::npos) return true;
    return false;
}

void LogBuffer::write(const char* line, uint32_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    buf_ += line;
    if (sync_left_) { --sync_left_; flush_unlocked(); last_ms_ = now_ms; return; }
    if (buf_.size() >= cap_ || now_ms - last_ms_ >= interval_ || critical(buf_)) {
        flush_unlocked();
        last_ms_ = now_ms;
    }
}

bool LogBuffer::tick(uint32_t now_ms) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || buf_.empty() || now_ms - last_ms_ < interval_) return false;
    flush_unlocked();
    last_ms_ = now_ms;
    return true;
}

void LogBuffer::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_unlocked();
}

void LogBuffer::flush_unlocked() {
    if (buf_.empty()) return;
    sink_(buf_);
    buf_.clear();
    ++flushes_;
}

uint64_t LogBuffer::flushes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flushes_;
}

}  // namespace hw::io
