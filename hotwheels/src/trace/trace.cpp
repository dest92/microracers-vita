// src/trace/trace.cpp — formateo puro de la traza (BUG-002, HW-007).
#include "trace/trace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hw::trace {

bool enabled() {
    const char* tr = std::getenv("HW_TRACE");
    return tr && *tr;
}

bool wants(const std::string& func) {
    const char* tr = std::getenv("HW_TRACE");
    if (!tr || !*tr) return false;
    const std::string t = tr;
    if (t == "*") return true;
    // Lista separada por comas; acepta tanto el nombre como "Dll.Func".
    const std::string name = func.substr(func.find_last_of('.') + 1);
    size_t pos = 0;
    while (pos <= t.size()) {
        const size_t comma = t.find(',', pos);
        std::string tok = t.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!tok.empty() && (tok == func || tok == name)) return true;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return false;
}

namespace {

std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o;
}

// Si [va, va+max) es ASCII imprimible terminado en NUL con >=2 chars, lo devuelve.
bool ascii_string(uint32_t va, const std::function<bool(uint32_t, uint8_t*, uint32_t)>& read,
    std::string& out) {
    if (va < 0x10000 || !read) return false;
    char buf[65];
    if (!read(va, reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    size_t n = 0;
    for (; n < sizeof(buf) - 1; ++n) {
        const unsigned char c = static_cast<unsigned char>(buf[n]);
        if (c == 0) break;
        if (c < 0x20 || c > 0x7e) return false;
    }
    if (n < 2 || n == sizeof(buf) - 1) return false;
    out.assign(buf, n);
    return true;
}

}  // namespace

std::string format_call(const std::string& dll, const std::string& func,
    const std::vector<std::string>& args, const std::string& ret) {
    std::string o;
    if (!dll.empty()) { o += dll; o += '.'; }
    o += func;
    o += '(';
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) o += ',';
        o += args[i];
    }
    o += ')';
    if (!ret.empty()) { o += " = "; o += ret; }
    return o;
}

std::string format_from_memory(const std::string& dll, const std::string& func,
    const uint32_t* args, int nargs, uint32_t ret,
    const std::function<bool(uint32_t, uint8_t*, uint32_t)>& read) {
    std::vector<std::string> toks;
    toks.reserve(nargs > 0 ? static_cast<size_t>(nargs) : 0);
    for (int i = 0; i < nargs; ++i) {
        const uint32_t v = args[i];
        std::string s;
        if (ascii_string(v, read, s)) {
            toks.push_back("\"" + escape(s) + "\"");
        } else if (v >= 0x10000) {
            toks.push_back("P");
        } else {
            char b[16];
            std::snprintf(b, sizeof b, "0x%x", v);
            toks.push_back(b);
        }
    }
    char r[16];
    std::snprintf(r, sizeof r, "0x%08x", ret);
    return format_call(dll, func, toks, r);
}

void emit(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace hw::trace
