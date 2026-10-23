#pragma once

namespace hw::runtime {

template <typename SetDefault>
void apply_console_release_defaults(SetDefault set_default) {
    set_default("WX86_LOGASYNC", "1");
    set_default("WX86_NOCANCEL", "1");
    set_default("WX86_WAITROUND", "1");
    set_default("WX86_COEURS", "222");
    // The game has no identified guest server thread; keep D2Vita migration off.
    set_default("WX86_COEUR_SERVEUR", "0");
}

} // namespace hw::runtime
