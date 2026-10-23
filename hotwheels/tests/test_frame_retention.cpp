#include "host/frame_retention.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;
static const fs::path root = "build/test-frame-retention";

static bool emit(const fs::path& dir, int count) {
    fs::create_directories(dir);
    for (int i = 0; i < count; ++i) {
        char name[40]; std::snprintf(name, sizeof name, "frame_%05d.png", i);
        const auto path = (dir / name).string();
        if (!hw::host::FrameRetention::save(path, [](const std::string& target) {
                std::ofstream file(target, std::ios::binary);
                file << 'x';
                return bool(file);
            })) return false;
    }
    return true;
}

static std::set<int> saved(const fs::path& dir) {
    std::set<int> result;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("frame_", 0) != 0 || entry.path().extension() != ".png") continue;
        if (name.size() < 15 || name.substr(0, 12) == "frame_recent") continue;
        result.insert(std::stoi(name.substr(6, name.size() - 10)));
    }
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    if (std::string(argv[1]) == "emit") {
        fs::remove_all(root);
        unsetenv("HW_FRAMES_ALL");
        assert(emit(root / "sampled", 101));
        setenv("HW_FRAMES_ALL", "1", 1);
        assert(emit(root / "all", 100));
        return 0;
    }
    assert(std::string(argv[1]) == "verify");
    std::set<int> expected{0, 50};
    for (int i = 81; i <= 100; ++i) expected.insert(i);
    assert(saved(root / "sampled") == expected);
    assert(saved(root / "all").size() == 100);
    return 0;
}
