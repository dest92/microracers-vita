#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace hw::host {

// Keep every 50th numbered frame and a disk-backed 20-frame rolling tail.
// The global lock serializes slot replacement across the SW and D3D presenters.
class FrameRetention {
public:
    static bool save(const std::string& path,
                     const std::function<bool(const std::string&)>& writer) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t frame = 0;
        if (!frame_index(path, frame) || all_frames()) return writer(path);

        const bool sampled = frame % 50 == 0;
        if (sampled && !writer(path)) return false;

        const auto dir = std::filesystem::path(path).parent_path();
        const unsigned slot = frame % 20;
        std::ostringstream prefix, temp;
        prefix << "frame_recent_" << std::setw(2) << std::setfill('0') << slot << "_";
        temp << prefix.str() << std::setw(5) << std::setfill('0') << frame << ".png";
        const auto recent = dir / temp.str();
        if (!writer(recent.string())) {
            std::error_code ignored;
            std::filesystem::remove(recent, ignored);
            return false;
        }
        std::error_code ignored;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const auto name = entry.path().filename().string();
            if (name.rfind(prefix.str(), 0) == 0 && entry.path() != recent)
                std::filesystem::remove(entry.path(), ignored);
        }
        return true;
    }

    static bool frame_index(const std::string& path, uint32_t& frame) {
        const auto name = std::filesystem::path(path).filename().string();
        if (name.rfind("frame_", 0) != 0 || name.size() < 15 ||
            name.substr(name.size() - 4) != ".png") return false;
        const auto digits = name.substr(6, name.size() - 10);
        if (digits.empty()) return false;
        for (char c : digits) if (c < '0' || c > '9') return false;
        try { frame = static_cast<uint32_t>(std::stoul(digits)); }
        catch (...) { return false; }
        return true;
    }

private:
    static bool all_frames() {
        const char* value = std::getenv("HW_FRAMES_ALL");
        return value && value[0] == '1' && value[1] == '\0';
    }
    inline static std::mutex mutex_;
};

}  // namespace hw::host
