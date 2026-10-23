#include "runtime/release_profile.h"

#include <cassert>
#include <cstdlib>
#include <map>
#include <string>

int main() {
    std::map<std::string, std::string> env{{"WX86_NOCANCEL", "0"}};
    hw::runtime::apply_console_release_defaults([&](const char* key, const char* value) {
        if (!env.count(key)) env[key] = value;
    });
    assert(env["WX86_LOGASYNC"] == "1");
    assert(env["WX86_NOCANCEL"] == "0"); // user env.txt remains authoritative
    assert(env["WX86_WAITROUND"] == "1");
    assert(env["WX86_COEURS"] == "222");
    assert(env["WX86_COEUR_SERVEUR"] == "0");
}
