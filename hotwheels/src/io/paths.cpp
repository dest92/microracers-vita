// src/io/paths.cpp — implementación de la política de rutas (HW-020).
#include "io/paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "shims/shims.h"

namespace fs = std::filesystem;

namespace hw::io {
namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
std::string basename(const std::string& p) {
    const size_t at = p.find_last_of("\\/");
    return at == std::string::npos ? p : p.substr(at + 1);
}
}  // namespace

Roots vita_roots(const std::string& data_root) {
    Roots r;
    r.game = data_root;
    r.cd = data_root;
    r.save = data_root + "/save";
    return r;
}

Roots host_roots(const std::string& extracted_root) {
    Roots r;
    r.game = extracted_root;
    r.cd = extracted_root;
    r.save = extracted_root + "/save";
    return r;
}

bool ensure_save(const Roots& r) {
    if (r.save.empty()) return true;
    std::error_code ec;
    fs::create_directories(r.save, ec);
    return !ec;
}

void mount(hw::shims::Vfs& vfs, const Roots& r) {
    vfs.mount('C', r.game, r.save);
    vfs.mount('D', r.cd);
    vfs.set_cwd("C:\\");
}

bool is_writable_game_file(const std::string& win_path) {
    const std::string leaf = lower(basename(win_path));
    return leaf == "settings.dat" || leaf == "highscore.dat" || leaf == "physics.dat";
}

}  // namespace hw::io
