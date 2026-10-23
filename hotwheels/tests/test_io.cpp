// tests/test_io.cpp — HW-020 (spec 010): CA1..CA6 del IO Vita (host).
#include "io/paths.h"
#include "io/read_cache.h"
#include "io/config.h"
#include "io/boot_menu.h"
#include "io/boot_selector.h"
#include "shims/shims.h"
#include <winx86-dx/io/vfs.h>
#include "dinput/dinput.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;
using namespace hw::io;
using hw::shims::Vfs;
static_assert(std::is_same_v<Vfs, winx86::dx::io::Vfs>, "Hot Wheels must consume the shared VFS type");

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++g_fail; } } while (0)

static void wfile(const std::string& p, const std::string& s) {
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream o(p, std::ios::binary); o << s;
}
static bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static std::string root = "build/test-io";

// ---- CA1: política de rutas ------------------------------------------------
static void ca1_paths() {
    Roots r = vita_roots("ux0:data/hotwheels");
    CHECK(r.game == "ux0:data/hotwheels", "CA1 game");
    CHECK(r.cd == r.game, "CA1 cd = game");
    CHECK(r.save == "ux0:data/hotwheels/save", "CA1 save");
    Vfs vfs;
    mount(vfs, r);
    CHECK(vfs.has_drive('C') && vfs.has_drive('D'), "CA1 C: y D: montadas");
    CHECK(vfs.cwd_drive() == 'C', "CA1 cwd C");
}

// ---- CA2: escrituras y case-insensitive (reusa Vfs) ------------------------
static void ca2_writable() {
    const std::string base = root + "/ca2/base", save = root + "/ca2/save";
    wfile(base + "/DATA/MENU/SETTINGS.DAT", "v1");
    wfile(base + "/DATA/LANG.TXT", "lang");
    Vfs vfs;
    vfs.mount('C', base, save);
    vfs.set_cwd("C:\\");

    auto rd = vfs.resolve_read("data\\menu\\settings.dat");
    CHECK(!rd.empty() && ends_with(rd, "SETTINGS.DAT"), "CA2 read case-insensitive");

    auto wr = vfs.resolve_write("data\\menu\\settings.dat");
    CHECK(!wr.empty() && wr.find(save) == 0, "CA2 write va a save");
    wfile(wr, "v2");
    auto rd2 = vfs.resolve_read("data\\menu\\settings.dat");
    CHECK(rd2 == wr, "CA2 read prefiere save");

    CHECK(is_writable_game_file("SETTINGS.DAT"), "CA2 settings escribible");
    CHECK(is_writable_game_file("highscore.dat"), "CA2 highscore escribible");
    CHECK(is_writable_game_file("physics.dat"), "CA2 physics escribible");
    CHECK(!is_writable_game_file("lang.txt"), "CA2 lang no escribible");
}

// ---- CA3: ReadCache --------------------------------------------------------
static void ca3_cache() {
    const std::string d = root + "/ca3";
    std::string small(100, 'a'), big(2000, 'b');
    wfile(d + "/small.bin", small);
    wfile(d + "/big.bin", big);

    ReadCache c;
    c.configure(1024, 4096);
    CHECK(c.load(d + "/small.bin"), "CA3 load chico");
    const std::vector<uint8_t>* got = c.get(d + "/small.bin");
    CHECK(got && got->size() == 100 && (*got)[0] == 'a', "CA3 get bytes");
    CHECK(!c.load(d + "/big.bin"), "CA3 no carga grande");
    CHECK(c.get(d + "/big.bin") == nullptr, "CA3 grande no está");
    CHECK(c.stats().files == 1 && c.stats().bytes == 100, "CA3 stats");
    CHECK(c.stats().bypasses >= 1, "CA3 bypass");

    // preload_dir sólo carga chicos.
    ReadCache c2;
    c2.configure(1024, 4096);
    size_t n = c2.preload_dir(d);
    CHECK(n == 1, "CA3 preload carga 1");
    CHECK(c2.stats().files == 1 && c2.stats().bytes == 100, "CA3 preload stats");
}

// ---- CA4/CA5: hwvita.ini ---------------------------------------------------
static void ca4_config() {
    const std::string ini =
        "; ejemplo\n"
        "[general]\n"
        "data_root=ux0:data/juego\n"
        "# comentario\n"
        "[display]\n"
        "scaling=stretch\n"
        "filter=linear\n"
        "[io]\n"
        "max_file_kb=512\n"
        "max_total_mb=8\n"
        "preload=0\n"
        "[buttons.pad0]\n"
        "cross=SPACE\n"
        "l1=RCONTROL\n"
        "circle=BACK\n"
        "[buttons.pad1]\n"
        "up=T\n";
    HwVitaConfig cfg;
    CHECK(parse(ini, cfg), "CA4 parse ok");
    CHECK(cfg.data_root == "ux0:data/juego", "CA4 data_root");
    CHECK(cfg.scaling == HwVitaConfig::Scaling::Stretch, "CA4 scaling");
    CHECK(cfg.filter == HwVitaConfig::Filter::Linear, "CA4 filter");
    CHECK(cfg.max_file_kb == 512 && cfg.max_total_mb == 8 && !cfg.preload, "CA4 io");
    using namespace hw::dinput;
    CHECK(cfg.btn_set[0][bi(Button::Cross)] && cfg.btn_dik[0][bi(Button::Cross)] == 0x39, "CA4 cross=SPACE");
    CHECK(cfg.btn_set[0][bi(Button::L1)] && cfg.btn_dik[0][bi(Button::L1)] == 0x9D, "CA4 l1=RCONTROL");
    CHECK(cfg.btn_set[0][bi(Button::Circle)] && cfg.btn_dik[0][bi(Button::Circle)] == 0x0E, "CA4 circle=BACK");
    CHECK(cfg.btn_set[1][bi(Button::DpadUp)] && cfg.btn_dik[1][bi(Button::DpadUp)] == 0x14, "CA4 up=T");

    MapperConfig m;
    apply_to(m, cfg);
    CHECK(m.pad[0].get(Button::Cross) == 0x39, "CA4 apply cross");
    CHECK(m.pad[0].get(Button::L1) == 0x9D, "CA4 apply l1");
    CHECK(m.pad[1].get(Button::DpadUp) == 0x14, "CA4 apply up");
}

static void ca5_defaults() {
    HwVitaConfig def;
    std::string t = defaults_ini();
    HwVitaConfig cfg;
    CHECK(parse(t, cfg), "CA5 parse template");
    CHECK(cfg.scaling == def.scaling && cfg.filter == def.filter, "CA5 template defaults");
    CHECK(cfg.max_file_kb == def.max_file_kb && cfg.max_total_mb == def.max_total_mb, "CA5 template io");
    for (int p = 0; p < 2; ++p)
        for (int b = 0; b < hw::dinput::kButtonCount; ++b)
            CHECK(cfg.btn_set[p][b] == false, "CA5 template sin overrides");

    HwVitaConfig bad;
    CHECK(parse("[display]\nscaling=banana\nfilter=zzz\nnokey=1\n", bad), "CA5 parse inválido no rompe");
    CHECK(bad.scaling == HwVitaConfig::Scaling::Fit, "CA5 inválido deja default");
    CHECK(bad.filter == HwVitaConfig::Filter::Point, "CA5 inválido filtro default");
    CHECK(!bad.warnings.empty(), "CA5 warning");

    HwVitaConfig dz;
    CHECK(parse("[general]\nstick_deadzone=4242\n", dz), "CA5 deadzone parse");
    CHECK(dz.stick_deadzone == 4242, "CA5 deadzone valor");
    HwVitaConfig dzbig;
    CHECK(parse("[general]\nstick_deadzone=9999999999\n", dzbig), "CA5 deadzone grande parse");
    CHECK(dzbig.stick_deadzone == 32767, "CA5 deadzone clamp 32767");
    HwVitaConfig dzneg;
    CHECK(parse("[general]\nstick_deadzone=18446744073709551615\n", dzneg), "CA5 deadzone u64max parse");
    CHECK(dzneg.stick_deadzone == 32767, "CA5 deadzone u64max clamp");
}

// ---- CA6: integración-lite mount + preload ---------------------------------
static void ca6_integration() {
    const std::string ext = root + "/ca6";
    wfile(ext + "/DATA/MENU/SETTINGS.DAT", "x");
    wfile(ext + "/DATA/LANG.TXT", "lang!");
    wfile(ext + "/DATA/MUSIC/BIG.BIN", std::string(4096, 'z'));
    Roots r = host_roots(ext);
    CHECK(ensure_save(r), "CA6 ensure save");
    Vfs vfs;
    mount(vfs, r);
    CHECK(!vfs.resolve_read("DATA\\MENU\\SETTINGS.DAT").empty(), "CA6 mount resuelve");

    ReadCache c;
    c.configure(1024, 1 << 20);
    size_t n = c.preload_dir(ext + "/DATA", true);
    CHECK(n == 2, "CA6 preload 2 chicos");
    CHECK(c.get(ext + "/DATA/MENU/SETTINGS.DAT") != nullptr, "CA6 chico en cache");
    CHECK(c.get(ext + "/DATA/MUSIC/BIG.BIN") == nullptr, "CA6 grande fuera");
    CHECK(c.stats().bytes == 1 + 5, "CA6 bytes medidos");
}

// ---- CA7/CA8: fps_mode y control_scheme ------------------------------------
static void ca7_fps_scheme() {
    HwVitaConfig cfg;
    CHECK(parse("[display]\nfps_mode=interp60\n[general]\ncontrol_scheme=swapped\n", cfg), "CA7 parse");
    CHECK(cfg.fps_mode == HwVitaConfig::FpsMode::Interp60, "CA7 fps interp60");
    CHECK(cfg.warnings.empty(), "SPEC-056 CA3 fps_mode viejo sin warning");
    CHECK(cfg.control_scheme == "swapped", "CA7 scheme swapped");
}

static void ca8_fps_invalid() {
    HwVitaConfig cfg;
    parse("[display]\nfps_mode=banana\n", cfg);
    CHECK(cfg.fps_mode == HwVitaConfig::FpsMode::Original, "CA8 fps inválido default");
    CHECK(!cfg.warnings.empty(), "CA8 warning fps");
}

// ---- CA9: selector de arranque ---------------------------------------------
static void ca9_boot_menu() {
    HwVitaConfig cfg;
    BootMenu m(cfg);
    // SPEC-056 CA2: el selector no ofrece "FPS mode" (el runtime corre siempre en original).
    CHECK(m.count() == 3 && m.selected() == 0, "CA9 items");
    for (int i = 0; i < m.count(); ++i) CHECK(m.label(i) != "FPS mode", "CA9 sin FPS mode");

    m.handle(MenuAction::Right);
    CHECK(cfg.control_scheme == "swapped", "CA9 scheme -> swapped");
    m.handle(MenuAction::Down);
    CHECK(m.selected() == 1, "CA9 down");
    m.handle(MenuAction::Right);
    CHECK(cfg.scaling == HwVitaConfig::Scaling::Stretch, "CA9 scaling");
    m.handle(MenuAction::Down);
    m.handle(MenuAction::Right);
    CHECK(cfg.filter == HwVitaConfig::Filter::Linear, "CA9 filter");
    CHECK(cfg.fps_mode == HwVitaConfig::FpsMode::Original, "CA9 fps intacto");

    auto lines = m.lines();
    CHECK(lines.size() >= 5, "CA9 lines");
    CHECK(lines[3].rfind(">", 0) == 0, "CA9 marcador en item 2");
    CHECK(m.handle(MenuAction::Confirm) == MenuResult::SaveAndExit, "CA9 confirm");
    CHECK(m.handle(MenuAction::Cancel) == MenuResult::CancelExit, "CA9 cancel");
}

// ---- CA10: guardar el estado del selector ----------------------------------
static void ca10_save_roundtrip() {
    HwVitaConfig cfg;
    cfg.fps_mode = HwVitaConfig::FpsMode::Native60;
    cfg.control_scheme = "swapped";
    cfg.scaling = HwVitaConfig::Scaling::Stretch;
    cfg.filter = HwVitaConfig::Filter::Linear;
    const std::string p = root + "/ca10/hwvita.ini";
    CHECK(save(p, cfg), "CA10 save");
    HwVitaConfig r;
    CHECK(load(p, r), "CA10 load");
    CHECK(r.fps_mode == HwVitaConfig::FpsMode::Native60, "CA10 fps");
    CHECK(r.control_scheme == "swapped", "CA10 scheme");
    CHECK(r.scaling == HwVitaConfig::Scaling::Stretch, "CA10 scaling");
    CHECK(r.filter == HwVitaConfig::Filter::Linear, "CA10 filter");
}

static void ca13_render_scale() {
    HwVitaConfig cfg;
    CHECK(cfg.render_scale == HwVitaConfig::RenderScale::Native, "CA13 default render_scale native");
    CHECK(parse("[display]\nrender_scale=original\nscaling=stretch\n", cfg), "CA13 parse render_scale");
    CHECK(cfg.render_scale == HwVitaConfig::RenderScale::Original, "CA13 original");
    CHECK(cfg.scaling == HwVitaConfig::Scaling::Stretch, "CA13 scaling preserved");
    CHECK(parse("[display]\nrender_scale=native\n", cfg), "CA13 parse native");
    CHECK(cfg.render_scale == HwVitaConfig::RenderScale::Native, "CA13 native");
    CHECK(parse("[display]\nrender_scale=unknown\n", cfg), "CA13 invalid value is nonfatal");
    CHECK(cfg.render_scale == HwVitaConfig::RenderScale::Native, "CA13 invalid keeps default");
    CHECK(!cfg.warnings.empty(), "CA13 invalid warns");
    cfg.render_scale = HwVitaConfig::RenderScale::Original;
    CHECK(to_ini(cfg).find("render_scale=original") != std::string::npos, "CA13 serialize render_scale");
}

static void ca14_d3d_filter_policy() {
    HwVitaConfig cfg;
    CHECK(cfg.filter == HwVitaConfig::Filter::Point, "CA14 sharp point filter is default");
    CHECK(parse("[display]\nfilter=linear\n", cfg), "CA14 parse linear");
    CHECK(cfg.filter == HwVitaConfig::Filter::Linear, "CA14 linear override");
    CHECK(parse("[display]\nfilter=game\n", cfg), "CA14 parse game");
    CHECK(cfg.filter == HwVitaConfig::Filter::Game, "CA14 game sampler override");
    CHECK(parse("[display]\nfilter=bad\n", cfg), "CA14 invalid is nonfatal");
    CHECK(cfg.filter == HwVitaConfig::Filter::Point && !cfg.warnings.empty(), "CA14 invalid retains sharp default");
    cfg.filter = HwVitaConfig::Filter::Game;
    CHECK(to_ini(cfg).find("filter=game") != std::string::npos, "CA14 serializes game filter");
}

static void ca15_force_d3d_settings() {
    HwVitaConfig cfg;
    CHECK(cfg.force_d3d, "CA15 force_d3d default on");
    CHECK(parse("[display]\nforce_d3d=0\n", cfg) && !cfg.force_d3d, "CA15 opt out parsed");
    CHECK(parse("[display]\nforce_d3d=invalid\n", cfg) && cfg.force_d3d && !cfg.warnings.empty(),
          "CA15 invalid force_d3d warns and keeps default");
    cfg.force_d3d = true;
    CHECK(to_ini(cfg).find("force_d3d=1") != std::string::npos, "CA15 serialized");

    const std::string game = root + "/ca15/game";
    const std::string save = root + "/ca15/save";
    wfile(game + "/DATA/MENU/settings.dat", "0\r\n2\r\n19\r\n");
    CHECK(apply_display_defaults(game, save, true), "CA15 update game settings");
    std::ifstream in(save + "/DATA/MENU/settings.dat", std::ios::binary);
    std::string actual((std::istreambuf_iterator<char>(in)), {});
    CHECK(actual == "1\r\n2\r\n19\r\n", "CA15 copies to save and changes first integer only");

    // A pre-existing overlay is authoritative even when its directory casing differs.
    const std::string mixed_save = root + "/ca15/mixed-save";
    wfile(mixed_save + "/Data/Menu/settings.dat", "0\n1\n19\n");
    CHECK(apply_display_defaults(game, mixed_save, true), "CA15 update existing mixed-case save");
    std::ifstream mixed(mixed_save + "/Data/Menu/settings.dat", std::ios::binary);
    actual.assign((std::istreambuf_iterator<char>(mixed)), {});
    CHECK(actual == "1\n1\n19\n", "CA15 save wins and keeps resolution and later settings");
    CHECK(!fs::exists(mixed_save + "/DATA/MENU/settings.dat"), "CA15 does not create case-variant save tree");

    wfile(save + "/DATA/MENU/settings.dat", "0\n2\n19\n");
    CHECK(apply_display_defaults(game, save, false), "CA15 disabled is accepted");
    std::ifstream off(save + "/DATA/MENU/settings.dat", std::ios::binary);
    actual.assign((std::istreambuf_iterator<char>(off)), {});
    CHECK(actual == "0\n2\n19\n", "CA15 disabled preserves software preference");

    const std::string empty_game = root + "/ca15/empty-game";
    const std::string empty_save = root + "/ca15/empty-save";
    wfile(empty_game + "/Data/Menu/settings.dat", "");
    CHECK(apply_display_defaults(empty_game, empty_save, true), "CA15 empty settings seed default");
    std::ifstream empty(empty_save + "/DATA/MENU/settings.dat", std::ios::binary);
    actual.assign((std::istreambuf_iterator<char>(empty)), {});
    CHECK(actual == "1\r\n2\r\n", "CA15 empty settings use explicit D3D/800 default");

    const std::string absent_game = root + "/ca15/absent-game";
    const std::string absent_save = root + "/ca15/absent-save";
    fs::create_directories(absent_game);
    CHECK(apply_display_defaults(absent_game, absent_save, true), "CA15 absent settings seed default");
    std::ifstream absent(absent_save + "/DATA/MENU/settings.dat", std::ios::binary);
    actual.assign((std::istreambuf_iterator<char>(absent)), {});
    CHECK(actual == "1\r\n2\r\n", "CA15 absent settings seed D3D/800");

    const std::string padded_game = root + "/ca15/padded-game";
    const std::string padded_save = root + "/ca15/padded-save";
    wfile(padded_game + "/DATA/MENU/settings.dat", " \t0 \t\n2\n");
    CHECK(apply_display_defaults(padded_game, padded_save, true), "CA15 accepts padded integer");
    std::ifstream padded(padded_save + "/DATA/MENU/settings.dat", std::ios::binary);
    actual.assign((std::istreambuf_iterator<char>(padded)), {});
    CHECK(actual == " \t1 \t\n2\n", "CA15 preserves surrounding whitespace");
    CHECK(!fs::exists(padded_save + "/DATA/MENU/settings.dat.hwvita-tmp"), "CA15 leaves no temp after atomic rename");

    const std::string no_eol_game = root + "/ca15/no-eol-game";
    const std::string no_eol_save = root + "/ca15/no-eol-save";
    wfile(no_eol_game + "/DATA/MENU/settings.dat", "0");
    CHECK(apply_display_defaults(no_eol_game, no_eol_save, true), "CA15 first integer without EOL");
    std::ifstream no_eol(no_eol_save + "/DATA/MENU/settings.dat", std::ios::binary);
    actual.assign((std::istreambuf_iterator<char>(no_eol)), {});
    CHECK(actual == "1", "CA15 preserves no-EOL layout");

    const std::string oversized_game = root + "/ca15/oversized-game";
    const std::string oversized_save = root + "/ca15/oversized-save";
    wfile(oversized_game + "/DATA/MENU/settings.dat", std::string(4097, '0'));
    CHECK(!apply_display_defaults(oversized_game, oversized_save, true), "CA15 oversized settings rejected");
    CHECK(!fs::exists(oversized_save), "CA15 oversized file is rejected before save allocation");
    const std::string malformed_game = root + "/ca15/malformed-game";
    const std::string malformed_save = root + "/ca15/malformed-save";
    for (const char* malformed : {"not-an-integer\n2\n", "1abc\n2\n", "0 x\n2\n"}) {
        wfile(malformed_game + "/DATA/MENU/settings.dat", malformed);
        CHECK(!apply_display_defaults(malformed_game, malformed_save, true), "CA15 malformed settings rejected");
        CHECK(!fs::exists(malformed_save), "CA15 malformed settings remain untouched");
    }
}

// ---- CA11: presets de mapa -------------------------------------------------
static void ca11_schemes() {
    using namespace hw::dinput;
    // Los presets y DIK concretos son política Hot Wheels, fuera del core genérico.
    MapperConfig m0;
    apply_control_scheme(m0, "original");
    CHECK(m0.pad[0].get(Button::Cross) == dik::RETURN, "CA11 original pad0");
    CHECK(m0.pad[0].get(Button::DpadUp) == dik::UP, "CA11 original pad0 dpad");
    CHECK(m0.pad[1].get(Button::Cross) == dik::LSHIFT, "CA11 original pad1");
    CHECK(m0.pad[1].get(Button::DpadUp) == dik::T, "CA11 original pad1 dpad");
    MapperConfig m1;
    apply_control_scheme(m1, "swapped");
    CHECK(m1.pad[0].get(Button::Cross) == dik::LSHIFT, "CA11 swapped pad0");
    CHECK(m1.pad[1].get(Button::Cross) == dik::RETURN, "CA11 swapped pad1");
}

// ---- CA12: apertura del selector -------------------------------------------
static void ca12_boot_open() {
    unsetenv("HW_CFG");
    CHECK(!boot_should_open(), "CA12 sin env no abre");
    setenv("HW_CFG", "1", 1);
    CHECK(boot_should_open(), "CA12 HW_CFG=1 abre");
    unsetenv("HW_CFG");
}

int main() {
    std::system("mkdir -p build && rm -rf build/test-io");
    ca1_paths();
    ca2_writable();
    ca3_cache();
    ca4_config();
    ca5_defaults();
    ca6_integration();
    ca7_fps_scheme();
    ca8_fps_invalid();
    ca9_boot_menu();
    ca10_save_roundtrip();
    ca13_render_scale();
    ca14_d3d_filter_policy();
    ca15_force_d3d_settings();
    ca11_schemes();
    ca12_boot_open();
    if (g_fail) { std::printf("IO: %d FALLAS\n", g_fail); return 1; }
    std::printf("IO: PASS (CA1..CA15)\n");
    return 0;
}
