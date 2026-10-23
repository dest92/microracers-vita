#include <winx86-dx/io/vfs.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace winx86::dx::io;
namespace fs = std::filesystem;

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("winx86-dx-vfs-" + std::to_string(nonce));
    const fs::path base = root / "base";
    const fs::path save = root / "save";
    fs::create_directories(base / "Data" / "Maps");
    fs::create_directories(save);
    {
        std::ofstream f(base / "Data" / "Maps" / "track.DAT");
        f << "base";
    }

    Vfs vfs;
    vfs.mount('c', base.string(), save.string());
    vfs.set_cwd("C:\\Data");
    assert(vfs.has_drive('C'));
    assert(vfs.cwd_drive() == 'C');
    assert(vfs.resolve_read("maps\\TRACK.dat") == (base / "Data" / "Maps" / "track.DAT").string());

    const auto write_path = vfs.resolve_write("C:\\Data\\Maps\\track.DAT");
    assert(!write_path.empty());
    {
        std::ofstream f(write_path);
        f << "save";
    }
    assert(vfs.resolve_read("C:\\Data\\Maps\\TRACK.dat") == write_path);
    auto entries = vfs.find("C:\\Data\\Maps\\*.dat");
    assert(entries.size() == 1 && entries[0].name == "track.DAT" && !entries[0].dir);
    assert(vfs.remove("C:\\Data\\Maps\\track.DAT"));
    assert(vfs.resolve_read("C:\\Data\\Maps\\track.DAT") == (base / "Data" / "Maps" / "track.DAT").string());

    const auto drives = vfs.drive_letters();
    assert(drives.size() == 1 && drives[0] == 'C');
    fs::remove_all(root);
}
