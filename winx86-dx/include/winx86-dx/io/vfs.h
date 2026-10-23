// Win32-style drive and path resolution over host directories.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace winx86::dx::io {

class Vfs {
public:
    struct Entry { std::string name, host; bool dir = false; uint64_t size = 0; };

    // Mount roots and drive letters come from the consuming game's config.
    // This mutable instance has no internal lock; the consumer serializes calls.
    void mount(char letter, std::string base, std::string save = "");
    void set_cwd(const std::string& win);
    bool has_drive(char letter) const;
    char cwd_drive() const { return cwd_drive_; }
    std::string resolve_read(const std::string& win) const;
    std::string resolve_write(const std::string& win) const;
    std::vector<Entry> find(const std::string& pattern) const;
    bool remove(const std::string& win);
    bool rename(const std::string& from, const std::string& to);
    std::vector<char> drive_letters() const;

private:
    struct Drive { std::string base, save; };
    bool split(const std::string& win, char& letter, std::vector<std::string>& comps) const;
    static std::string lookup(const std::string& root, const std::vector<std::string>& comps,
                              size_t n);
    std::string save_path(const std::string& win) const;
    std::map<char, Drive> drives_;
    char cwd_drive_ = 'C';
    std::vector<std::string> cwd_;
};

}  // namespace winx86::dx::io
