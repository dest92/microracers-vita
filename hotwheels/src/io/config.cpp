// src/io/config.cpp — parser de hwvita.ini (HW-020, spec 010).
#include "io/config.h"
#include "io/paths.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
using hw::dinput::Button;
using hw::dinput::bi;

namespace hw::io {
namespace {

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
bool parse_bool(const std::string& v, bool& out) {
    const std::string s = lower(trim(v));
    if (s == "1" || s == "true" || s == "yes" || s == "on") { out = true; return true; }
    if (s == "0" || s == "false" || s == "no" || s == "off") { out = false; return true; }
    return false;
}
bool parse_u64(const std::string& v, uint64_t& out) {
    const std::string s = trim(v);
    if (s.empty()) return false;
    char* end = nullptr;
    const unsigned long long x = std::strtoull(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = x; return true;
}

const std::map<std::string, Button>& button_table() {
    static const std::map<std::string, Button> t = {
        {"up", Button::DpadUp}, {"down", Button::DpadDown},
        {"left", Button::DpadLeft}, {"right", Button::DpadRight},
        {"cross", Button::Cross}, {"circle", Button::Circle},
        {"square", Button::Square}, {"triangle", Button::Triangle},
        {"l1", Button::L1}, {"l2", Button::L2}, {"r1", Button::R1}, {"r2", Button::R2},
        {"start", Button::Start}, {"select", Button::Select},
        {"lstick_up", Button::LStickUp}, {"lstick_down", Button::LStickDown},
        {"lstick_left", Button::LStickLeft}, {"lstick_right", Button::LStickRight},
        {"rstick_up", Button::RStickUp}, {"rstick_down", Button::RStickDown},
        {"rstick_left", Button::RStickLeft}, {"rstick_right", Button::RStickRight},
    };
    return t;
}

// Scancodes DIK estándar (MinGW dinput.h). Coinciden con hw::dinput::dik.
const std::map<std::string, uint8_t>& dik_table() {
    static const std::map<std::string, uint8_t> t = {
        {"ESCAPE", 0x01}, {"SPACE", 0x39}, {"RETURN", 0x1C}, {"TAB", 0x0F},
        {"BACK", 0x0E}, {"PAUSE", 0xC5},
        {"LSHIFT", 0x2A}, {"RSHIFT", 0x36}, {"LCONTROL", 0x1D}, {"RCONTROL", 0x9D},
        {"LALT", 0x38}, {"RALT", 0xB8},
        {"UP", 0xC8}, {"DOWN", 0xD0}, {"LEFT", 0xCB}, {"RIGHT", 0xCD},
        {"A", 0x1E}, {"B", 0x30}, {"C", 0x2E}, {"D", 0x20}, {"E", 0x12}, {"F", 0x21},
        {"G", 0x22}, {"H", 0x23}, {"I", 0x17}, {"J", 0x24}, {"K", 0x25}, {"L", 0x26},
        {"M", 0x32}, {"N", 0x31}, {"O", 0x18}, {"P", 0x19}, {"Q", 0x10}, {"R", 0x13},
        {"S", 0x1F}, {"T", 0x14}, {"U", 0x16}, {"V", 0x2F}, {"W", 0x11}, {"X", 0x2D},
        {"Y", 0x15}, {"Z", 0x2C},
    };
    return t;
}

void apply_kv(HwVitaConfig& o, const std::string& section, const std::string& key, const std::string& val) {
    if (section == "general") {
        if (key == "data_root") { o.data_root = trim(val); return; }
        if (key == "control_scheme") {
            const std::string s = lower(trim(val));
            if (s.empty()) { o.warnings.push_back("control_scheme vacío"); return; }
            o.control_scheme = s;
            const auto names = scheme_names();
            if (std::find(names.begin(), names.end(), s) == names.end())
                o.warnings.push_back("control_scheme desconocido: " + s);
            return;
        }
        if (key == "mouse_from_right_stick") { bool b; if (parse_bool(val, b)) { o.mouse_from_right_stick = b; return; } }
        if (key == "stick_deadzone") { uint64_t n; if (parse_u64(val, n)) { o.stick_deadzone = (int)std::min<uint64_t>(n, 32767); return; } }
    } else if (section == "display") {
        if (key == "force_d3d") {
            bool b;
            if (parse_bool(val, b)) { o.force_d3d = b; return; }
        } else if (key == "fps_mode") {
            HwVitaConfig::FpsMode m;
            if (fps_mode_parse(val, m)) { o.fps_mode = m; return; }
        } else if (key == "scaling") {
            const std::string s = lower(trim(val));
            if (s == "fit") { o.scaling = HwVitaConfig::Scaling::Fit; return; }
            if (s == "stretch") { o.scaling = HwVitaConfig::Scaling::Stretch; return; }
        } else if (key == "render_scale") {
            const std::string s = lower(trim(val));
            if (s == "original") { o.render_scale = HwVitaConfig::RenderScale::Original; return; }
            if (s == "native") { o.render_scale = HwVitaConfig::RenderScale::Native; return; }
        } else if (key == "filter") {
            const std::string s = lower(trim(val));
            if (s == "point") { o.filter = HwVitaConfig::Filter::Point; return; }
            if (s == "linear") { o.filter = HwVitaConfig::Filter::Linear; return; }
            if (s == "game") { o.filter = HwVitaConfig::Filter::Game; return; }
        }
    } else if (section == "io") {
        if (key == "preload") { bool b; if (parse_bool(val, b)) { o.preload = b; return; } }
        if (key == "max_file_kb") { uint64_t n; if (parse_u64(val, n)) { o.max_file_kb = (size_t)n; return; } }
        if (key == "max_total_mb") { uint64_t n; if (parse_u64(val, n)) { o.max_total_mb = (size_t)n; return; } }
    } else if (section == "buttons.pad0" || section == "buttons.pad1") {
        const int pad = section.back() == '0' ? 0 : 1;
        auto bit = button_table().find(key);
        if (bit == button_table().end()) { o.warnings.push_back("botón desconocido: " + key); return; }
        std::string v = trim(val);
        for (auto& ch : v) ch = char(std::toupper((unsigned char)ch));
        auto kit = dik_table().find(v);
        if (kit == dik_table().end()) {
            // permitir hex 0xNN
            const std::string s = trim(val);
            if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                char* end = nullptr;
                const unsigned long long n = std::strtoull(s.c_str() + 2, &end, 16);
                if (end && *end == '\0' && n <= 0xFF) {
                    o.btn_set[pad][bi(bit->second)] = true;
                    o.btn_dik[pad][bi(bit->second)] = (uint8_t)n;
                    return;
                }
            }
            o.warnings.push_back("tecla DIK desconocida: " + val);
            return;
        }
        o.btn_set[pad][bi(bit->second)] = true;
        o.btn_dik[pad][bi(bit->second)] = kit->second;
        return;
    }
    o.warnings.push_back("clave desconocida [" + section + "] " + key);
}

}  // namespace

const char* fps_mode_name(HwVitaConfig::FpsMode m) {
    switch (m) {
        case HwVitaConfig::FpsMode::Interp60: return "interp60";
        case HwVitaConfig::FpsMode::Native60: return "native60";
        default: return "original";
    }
}
bool fps_mode_parse(const std::string& s, HwVitaConfig::FpsMode& out) {
    const std::string v = lower(trim(s));
    if (v == "original") { out = HwVitaConfig::FpsMode::Original; return true; }
    if (v == "interp60") { out = HwVitaConfig::FpsMode::Interp60; return true; }
    if (v == "native60") { out = HwVitaConfig::FpsMode::Native60; return true; }
    return false;
}

std::vector<std::string> scheme_names() {
    // Set mínimo; HW-024 (deepseek) extiende/reemplaza. "swapped" intercambia los
    // dos pads (J1/J2) por si el juego asigna los jugadores al revés.
    return {"original", "swapped"};
}

void apply_control_scheme(hw::dinput::MapperConfig& m, const std::string& scheme) {
    const bool swapped = lower(trim(scheme)) == "swapped";
    const hw::dinput::MapperConfig preset = hw::dinput::hotwheels_mapper(swapped);
    m.pad[0] = preset.pad[0];
    m.pad[1] = preset.pad[1];
}

bool parse(const std::string& text, HwVitaConfig& out) {
    out = HwVitaConfig{};
    std::string section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        if (eol == std::string::npos) pos = text.size() + 1; else pos = eol + 1;
        const size_t c = line.find_first_of(";#");
        if (c != std::string::npos) line = line.substr(0, c);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { out.warnings.push_back("línea sin '=': " + line); continue; }
        apply_kv(out, section, lower(trim(line.substr(0, eq))), trim(line.substr(eq + 1)));
    }
    return true;
}

bool load(const std::string& path, HwVitaConfig& out) {
    std::error_code ec;
    if (!fs::exists(path, ec)) { out = HwVitaConfig{}; return true; }
    std::ifstream in(path, std::ios::binary);
    if (!in) { out = HwVitaConfig{}; out.warnings.push_back("no se pudo abrir " + path); return true; }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(text, out);
}

std::string defaults_ini() {
    return
        "; hwvita.ini — configuración de Hot Wheels Micro Racers Vita (HW-020)\n"
        "; Este archivo no viene del juego. Borralo para volver a los defaults.\n"
        "\n"
        "[general]\n"
        "data_root=ux0:data/hotwheels\n"
        "control_scheme=original\n"
        "mouse_from_right_stick=1\n"
        "stick_deadzone=8000\n"
        "\n"
        "[display]\n"
        "; se ignora: el runtime corre siempre en original (BUG-047, SPEC-056)\n"
        "fps_mode=original\n"
        "; fit = respeta 4:3 con barras; stretch = llena 960x544\n"
        "scaling=fit\n"
        "; native = XYZRHW mapea directo al viewport de salida; original = aspecto 4:3 legacy\n"
        "render_scale=native\n"
        "; point = nitido/GL_NEAREST; linear = GL_LINEAR; game = sampler D3D6\n"
        "filter=point\n"
        "; fuerza aceleración 3D en settings.dat; 0 conserva software\n"
        "force_d3d=1\n"
        "\n"
        "[io]\n"
        "; precarga DATA/ chico en RAM al inicio (evita tirones de IO)\n"
        "preload=1\n"
        "max_file_kb=2048\n"
        "max_total_mb=32\n"
        "\n"
        "[buttons.pad0]\n"
        "; up=UP\n"
        "; cross=RETURN\n"
        "; circle=BACK\n"
        "; l1=RSHIFT\n"
        "\n"
        "[buttons.pad1]\n"
        "; up=T\n"
        "; cross=LSHIFT\n"
        "; circle=LCONTROL\n";
}

bool write_default(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) return false;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    o << defaults_ini();
    return (bool)o;
}

void apply_to(hw::dinput::MapperConfig& m, const HwVitaConfig& cfg) {
    apply_control_scheme(m, cfg.control_scheme);
    for (int p = 0; p < 2; ++p)
        for (int b = 0; b < hw::dinput::kButtonCount; ++b)
            if (cfg.btn_set[p][b]) m.pad[p].set(static_cast<Button>(b), cfg.btn_dik[p][b]);
    m.mouse_from_right_stick = cfg.mouse_from_right_stick;
    m.stick_deadzone = cfg.stick_deadzone;
}

std::string to_ini(const HwVitaConfig& c) {
    std::ostringstream o;
    o << "; hwvita.ini (HW-020). Generado por el selector de arranque.\n";
    o << "[general]\n";
    o << "data_root=" << c.data_root << "\n";
    o << "control_scheme=" << c.control_scheme << "\n";
    o << "mouse_from_right_stick=" << (c.mouse_from_right_stick ? 1 : 0) << "\n";
    o << "stick_deadzone=" << c.stick_deadzone << "\n";
    o << "\n[display]\n";
    o << "fps_mode=" << fps_mode_name(c.fps_mode) << "\n";
    o << "scaling=" << (c.scaling == HwVitaConfig::Scaling::Stretch ? "stretch" : "fit") << "\n";
    o << "render_scale=" << (c.render_scale == HwVitaConfig::RenderScale::Native ? "native" : "original") << "\n";
    const char* filter = c.filter == HwVitaConfig::Filter::Linear ? "linear"
                       : c.filter == HwVitaConfig::Filter::Game ? "game" : "point";
    o << "filter=" << filter << "\n";
    o << "force_d3d=" << (c.force_d3d ? 1 : 0) << "\n";
    o << "\n[io]\n";
    o << "preload=" << (c.preload ? 1 : 0) << "\n";
    o << "max_file_kb=" << c.max_file_kb << "\n";
    o << "max_total_mb=" << c.max_total_mb << "\n";

    std::map<Button, std::string> rev;
    for (const auto& kv : button_table()) rev[kv.second] = kv.first;
    for (int p = 0; p < 2; ++p) {
        bool any = false;
        for (int b = 0; b < hw::dinput::kButtonCount; ++b) if (c.btn_set[p][b]) any = true;
        if (!any) continue;
        o << "\n[buttons.pad" << p << "]\n";
        for (int b = 0; b < hw::dinput::kButtonCount; ++b)
            if (c.btn_set[p][b]) o << rev[static_cast<Button>(b)] << "=0x"
                                   << std::hex << std::uppercase << (int)c.btn_dik[p][b] << std::dec << "\n";
    }
    return o.str();
}

bool save(const std::string& path, const HwVitaConfig& cfg) {
    std::error_code ec;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    o << to_ini(cfg);
    return (bool)o;
}

bool apply_display_defaults(const std::string& game_root, const std::string& save_root, bool force_d3d) {
    if (!force_d3d) return true;
    namespace fs = std::filesystem;
    std::error_code ec;
    hw::shims::Vfs vfs;
    vfs.mount('C', game_root, save_root);
    hw::shims::Vfs save_vfs;
    save_vfs.mount('C', save_root);
    const std::string guest_path = "C:\\DATA\\MENU\\settings.dat";
    const std::string save_file = save_vfs.resolve_read(guest_path);
    const std::string source = vfs.resolve_read(guest_path);
    const bool create_default = source.empty();
    fs::path target;
    std::string contents;
    if (!source.empty()) {
        const auto size = fs::file_size(source, ec);
        if (ec || size > 4096) return false;
        std::ifstream in(source, std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(in), {});
        if (!in.good() && !in.eof()) return false;
        if (!save_file.empty()) target = save_file;
    } else {
        contents = "1\r\n2\r\n";
    }
    if (contents.size() > 4096) return false;
    if (contents.empty()) contents = "0\r\n2\r\n";
    const size_t eol = contents.find_first_of("\r\n");
    const size_t end = eol == std::string::npos ? contents.size() : eol;
    size_t begin = 0;
    while (begin < end && (contents[begin] == ' ' || contents[begin] == '\t')) ++begin;
    size_t number_end = begin;
    if (number_end < end && (contents[number_end] == '+' || contents[number_end] == '-')) ++number_end;
    const size_t digits = number_end;
    while (number_end < end && contents[number_end] >= '0' && contents[number_end] <= '9') ++number_end;
    if (digits == number_end) return false;
    for (size_t i = number_end; i < end; ++i)
        if (contents[i] != ' ' && contents[i] != '\t') return false;
    if (contents.compare(begin, number_end - begin, "1") == 0 && !create_default) return true;
    contents.replace(begin, number_end - begin, "1");
    if (target.empty()) {
        fs::create_directories(save_root, ec);
        if (ec) return false;
        target = vfs.resolve_write(guest_path);
    }
    if (target.empty()) return false;
    fs::path temp = target;
    temp += ".hwvita-tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    if (!out || std::rename(temp.string().c_str(), target.string().c_str()) != 0) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

}  // namespace hw::io
