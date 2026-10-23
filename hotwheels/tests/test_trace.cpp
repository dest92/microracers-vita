// tests/test_trace.cpp — formato de traza de BUG-002 (HW-007).
//
// 1) Checks del formateo (detección de strings, punteros, retorno).
// 2) Emite una "corrida corta" re-formateando el prefijo de la referencia Wine
//    a través del módulo, en hotwheels/out (argv[1]); run_trace.sh luego corre
//    diff_trace.py sobre ese log y exige exit 0 (OK u OK parcial).
#include "trace/trace.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace hw::trace;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool starts_with_digits(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
    return i > 0 && i < s.size() && (s[i] == ' ' || s[i] == '\t');
}

static std::vector<std::string> split_top(const std::string& body) {
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (q) {
            cur += c;
            if (c == '\\' && i + 1 < body.size()) { cur += body[++i]; continue; }
            if (c == '"') q = false;
        } else if (c == '"') { q = true; cur += c; }
        else if (c == ',') { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    const std::string t = trim(cur);
    if (!t.empty() || !out.empty()) out.push_back(t);
    return out;
}

int main(int argc, char** argv) {
    // ---- 1) checks de formateo ----
    std::map<uint32_t, std::string> mem{{0x401000u, std::string("KERNEL32\0", 9)}};
    auto read = [&mem](uint32_t va, uint8_t* buf, uint32_t n) -> bool {
        for (const auto& kv : mem) {
            if (va >= kv.first && va < kv.first + kv.second.size()) {
                const uint32_t off = va - kv.first;
                uint32_t c = static_cast<uint32_t>(kv.second.size()) - off;
                if (c > n) c = n;
                std::memcpy(buf, kv.second.data() + off, c);
                if (c < n) buf[c] = 0;
                return true;
            }
        }
        return false;
    };
    {
        const uint32_t a1[1] = {0x401000u};
        const std::string s = format_from_memory("KERNEL32", "GetModuleHandleA", a1, 1, 0x400000u, read);
        CHECK(s.find("\"KERNEL32\"") != std::string::npos);
        CHECK(s.find("= 0x00400000") != std::string::npos);
        const uint32_t a2[1] = {0x12345678u};
        const std::string p = format_from_memory("KERNEL32", "CloseHandle", a2, 1, 1, read);
        CHECK(p.find("(P)") != std::string::npos);
        const std::string z = format_call("KERNEL32", "GetVersion", {}, "");
        CHECK(z == "KERNEL32.GetVersion()");
        const std::string q = format_call("KERNEL32", "GetDriveTypeA", {"\"C:\\\\\""}, "3");
        CHECK(q == "KERNEL32.GetDriveTypeA(\"C:\\\\\") = 3");
    }
    // Habilitación por HW_TRACE (lo usa el enganche de integ).
    {
        unsetenv("HW_TRACE");
        CHECK(!enabled() && !wants("GetVersion"));
        setenv("HW_TRACE", "*", 1);
        CHECK(enabled() && wants("Anything"));
        setenv("HW_TRACE", "GetVersion,CreateFileA", 1);
        CHECK(wants("KERNEL32.GetVersion") && wants("CreateFileA"));
        CHECK(!wants("CloseHandle"));
        unsetenv("HW_TRACE");
    }

    // ---- 2) "corrida corta": prefijo de la ref re-formateado por el módulo ----
    const char* outpath = argc > 1 ? argv[1] : "trace_short.log";
    const char* refpath = argc > 2 ? argv[2] : "docs/wine-ref/boot-trace.txt";
    const int maxcalls = argc > 3 ? atoi(argv[3]) : 20;

    std::ifstream in(refpath);
    if (!in) { std::printf("no existe ref %s\n", refpath); return 2; }
    std::ofstream out(outpath);
    if (!out) { std::printf("no puedo escribir %s\n", outpath); return 2; }

    std::string line;
    int emitted = 0;
    while (emitted < maxcalls && std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (starts_with_digits(line)) line = trim(line.substr(line.find(' ')));
        if (line.empty() || line[0] == '(') continue;  // grupos colapsados: no
        const size_t lp = line.find('(');
        const size_t rp = lp == std::string::npos ? std::string::npos : line.find(')', lp);
        if (lp == std::string::npos || rp == std::string::npos) continue;
        const std::string func = trim(line.substr(0, lp));
        const std::vector<std::string> args = split_top(line.substr(lp + 1, rp - lp - 1));
        std::string ret;
        const size_t eq = line.find('=', rp);
        if (eq != std::string::npos) {
            const std::string r = trim(line.substr(eq + 1));
            ret = r.substr(0, r.find(' '));  // cortar "@caller"
        }
        out << "[hw] " << format_call("", func, args, ret) << "\n";
        ++emitted;
    }
    out.close();
    std::printf("emitidas %d llamadas -> %s\n", emitted, outpath);
    CHECK(emitted >= 10);

    if (g_fail) { std::printf("TRACE-FMT: %d fallas\n", g_fail); return 1; }
    std::printf("TRACE-FMT: OK\n");
    return 0;
}
