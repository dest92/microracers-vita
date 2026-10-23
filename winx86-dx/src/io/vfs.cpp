// Portable Win32-style VFS over host filesystem roots.
#include "../../include/winx86-dx/io/vfs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <system_error>

namespace fs = std::filesystem;
namespace winx86::dx::io {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ieq(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

bool wild(const char* p, const char* s) {
    if (!*p) return !*s;
    if (*p == '*') return wild(p + 1, s) || (*s && wild(p, s + 1));
    if (!*s) return false;
    if (*p != '?' && std::tolower(static_cast<unsigned char>(*p))
            != std::tolower(static_cast<unsigned char>(*s))) return false;
    return wild(p + 1, s + 1);
}

}  // namespace

void Vfs::mount(char letter, std::string base, std::string save) {
    drives_[char(std::toupper(static_cast<unsigned char>(letter)))] = {std::move(base), std::move(save)};
}

bool Vfs::has_drive(char letter) const {
    return drives_.count(char(std::toupper(static_cast<unsigned char>(letter)))) != 0;
}

std::vector<char> Vfs::drive_letters() const {
    std::vector<char> result;
    for (const auto& [letter, drive] : drives_) {
        (void)drive;
        result.push_back(letter);
    }
    return result;
}

void Vfs::set_cwd(const std::string& win) {
    char letter;
    std::vector<std::string> components;
    if (split(win, letter, components)) {
        cwd_drive_ = letter;
        cwd_ = std::move(components);
    }
}

bool Vfs::split(const std::string& win, char& letter, std::vector<std::string>& components) const {
    std::string rest = win;
    letter = cwd_drive_;
    bool absolute = false;
    if (rest.size() >= 2 && std::isalpha(static_cast<unsigned char>(rest[0])) && rest[1] == ':') {
        letter = char(std::toupper(static_cast<unsigned char>(rest[0])));
        rest = rest.substr(2);
        components.clear();
        if (letter == cwd_drive_) components = cwd_;
        absolute = !rest.empty() && (rest[0] == '\\' || rest[0] == '/');
    } else {
        absolute = !rest.empty() && (rest[0] == '\\' || rest[0] == '/');
        components = cwd_;
    }
    if (absolute) components.clear();
    std::string current;
    auto flush = [&] {
        if (current.empty() || current == ".") {
        } else if (current == "..") {
            if (!components.empty()) components.pop_back();
        } else {
            components.push_back(current);
        }
        current.clear();
    };
    for (char c : rest) {
        if (c == '\\' || c == '/') flush();
        else current += c;
    }
    flush();
    return drives_.count(letter) != 0;
}

std::string Vfs::lookup(const std::string& root, const std::vector<std::string>& components, size_t n) {
    if (root.empty()) return "";
    fs::path path(root);
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return "";
    for (size_t i = 0; i < n; ++i) {
        fs::path exact = path / components[i];
        if (fs::exists(exact, ec)) {
            path = exact;
            continue;
        }
        bool found = false;
        for (auto& entry : fs::directory_iterator(path, ec)) {
            if (ieq(entry.path().filename().string(), components[i])) {
                path = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return "";
    }
    return path.string();
}

std::string Vfs::resolve_read(const std::string& win) const {
    char letter;
    std::vector<std::string> components;
    if (!split(win, letter, components)) return "";
    const auto& drive = drives_.at(letter);
    auto save = lookup(drive.save, components, components.size());
    return save.empty() ? lookup(drive.base, components, components.size()) : save;
}

std::string Vfs::save_path(const std::string& win) const {
    char letter;
    std::vector<std::string> components;
    if (!split(win, letter, components)) return "";
    return lookup(drives_.at(letter).save, components, components.size());
}

std::string Vfs::resolve_write(const std::string& win) const {
    char letter;
    std::vector<std::string> components;
    if (!split(win, letter, components) || components.empty()) return "";
    const auto& drive = drives_.at(letter);
    fs::path path(drive.save.empty() ? drive.base : drive.save);
    std::error_code ec;
    for (size_t i = 0; i < components.size(); ++i) {
        fs::path next = path / components[i];
        if (!fs::exists(next, ec)) {
            for (auto& entry : fs::directory_iterator(path, ec)) {
                if (ieq(entry.path().filename().string(), components[i])) {
                    next = entry.path();
                    break;
                }
            }
        }
        if (i + 1 < components.size() && !fs::exists(next, ec)) fs::create_directory(next, ec);
        path = next;
    }
    return path.string();
}

std::vector<Vfs::Entry> Vfs::find(const std::string& pattern) const {
    std::vector<Entry> result;
    char letter;
    std::vector<std::string> components;
    if (!split(pattern, letter, components) || components.empty()) return result;
    const std::string mask = components.back();
    const auto& drive = drives_.at(letter);
    std::set<std::string> seen;
    for (const auto* root : {&drive.save, &drive.base}) {
        const auto directory = lookup(*root, components, components.size() - 1);
        if (directory.empty()) continue;
        std::error_code ec;
        std::vector<fs::directory_entry> items(fs::directory_iterator(directory, ec), {});
        std::sort(items.begin(), items.end());
        for (auto& entry : items) {
            const auto name = entry.path().filename().string();
            if (!wild(mask.c_str(), name.c_str()) || !seen.insert(lower(name)).second) continue;
            Entry found{name, entry.path().string(), entry.is_directory(ec), 0};
            if (!found.dir) found.size = fs::file_size(entry.path(), ec);
            result.push_back(std::move(found));
        }
    }
    return result;
}

bool Vfs::remove(const std::string& win) {
    const auto path = save_path(win);
    std::error_code ec;
    return !path.empty() && fs::remove(path, ec);
}

bool Vfs::rename(const std::string& from, const std::string& to) {
    const auto source = resolve_read(from);
    const auto destination = resolve_write(to);
    if (source.empty() || destination.empty()) return false;
    std::error_code ec;
    if (source == save_path(from)) fs::rename(source, destination, ec);
    else fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

}  // namespace winx86::dx::io
