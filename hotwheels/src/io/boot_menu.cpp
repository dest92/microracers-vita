// src/io/boot_menu.cpp — implementación del selector de arranque (HW-020).
#include "io/boot_menu.h"

namespace hw::io {

int BootMenu::count() const { return kItemCount; }

std::string BootMenu::label(int i) const {
    switch (i) {
        case kScheme: return "Control scheme";
        case kScaling: return "Scaling";
        case kFilter: return "Filter";
        default: return "";
    }
}

std::string BootMenu::value(int i) const {
    switch (i) {
        case kScheme: return cfg_.control_scheme;
        case kScaling: return cfg_.scaling == HwVitaConfig::Scaling::Stretch ? "stretch" : "fit";
        case kFilter: return cfg_.filter == HwVitaConfig::Filter::Linear ? "linear"
                            : cfg_.filter == HwVitaConfig::Filter::Game ? "game" : "point";
        default: return "";
    }
}

void BootMenu::cycle(int dir) {
    const int d = dir < 0 ? -1 : 1;
    switch (sel_) {
        case kScheme: {
            const auto names = scheme_names();
            if (names.empty()) break;
            int idx = 0;
            for (size_t i = 0; i < names.size(); ++i) if (names[i] == cfg_.control_scheme) idx = (int)i;
            idx = (idx + d + (int)names.size()) % (int)names.size();
            cfg_.control_scheme = names[(size_t)idx];
            break;
        }
        case kScaling: {
            cfg_.scaling = cfg_.scaling == HwVitaConfig::Scaling::Fit
                ? HwVitaConfig::Scaling::Stretch : HwVitaConfig::Scaling::Fit;
            break;
        }
        case kFilter: {
            const int n = 3;
            int v = (static_cast<int>(cfg_.filter) + d + n) % n;
            cfg_.filter = static_cast<HwVitaConfig::Filter>(v);
            break;
        }
        default: break;
    }
}

MenuResult BootMenu::handle(MenuAction a) {
    switch (a) {
        case MenuAction::Up: sel_ = (sel_ - 1 + kItemCount) % kItemCount; return MenuResult::Continue;
        case MenuAction::Down: sel_ = (sel_ + 1) % kItemCount; return MenuResult::Continue;
        case MenuAction::Left: cycle(-1); return MenuResult::Continue;
        case MenuAction::Right: cycle(+1); return MenuResult::Continue;
        case MenuAction::Confirm: return MenuResult::SaveAndExit;
        case MenuAction::Cancel: return MenuResult::CancelExit;
    }
    return MenuResult::Continue;
}

std::vector<std::string> BootMenu::lines() const {
    std::vector<std::string> out;
    out.push_back("HotWheels Vita");
    for (int i = 0; i < kItemCount; ++i) {
        std::string l = (i == sel_) ? "> " : "  ";
        l += label(i);
        while (l.size() < 24) l += ' ';
        l += ": " + value(i);
        out.push_back(l);
    }
    out.push_back("X: save & play   O: cancel");
    return out;
}

}  // namespace hw::io
