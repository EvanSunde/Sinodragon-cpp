#include "keyboard_configurator/window_source.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace kb::cfg {

// Defined by the per-backend translation units.
std::unique_ptr<WindowSource> makeHyprlandWindowSource(const std::string& events_socket);
std::unique_ptr<WindowSource> makeSwayWindowSource(const std::string& socket_path);
std::unique_ptr<WindowSource> makeX11WindowSource();

std::vector<std::string> availableWindowSources() {
    std::vector<std::string> sources{"hyprland", "sway"};
#ifdef SINODRAGON_HAVE_X11
    sources.emplace_back("x11");
#endif
    return sources;
}

std::unique_ptr<WindowSource> createWindowSource(const std::string& preferred,
                                                 const std::string& events_socket) {
    std::string choice;
    choice.reserve(preferred.size());
    for (char ch : preferred) {
        choice.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (choice == "none" || choice == "off") {
        return nullptr;
    }

    if (choice == "hyprland") {
        return makeHyprlandWindowSource(events_socket);
    }
    if (choice == "sway" || choice == "i3") {
        return makeSwayWindowSource(events_socket);
    }
    if (choice == "x11") {
        auto source = makeX11WindowSource();
        if (!source) {
            std::cerr << "[Window] This build has no X11 support (libX11 was not found at build "
                         "time).\n";
        }
        return source;
    }
    if (!choice.empty() && choice != "auto") {
        std::cerr << "[Window] Unknown window_source '" << preferred << "'; falling back to auto.\n";
    }

    // Auto: pick whichever compositor is actually running. Hyprland and sway
    // are checked before X11 because both can also have DISPLAY set for
    // Xwayland, and their native protocols report app ids more accurately.
    if (hyprlandAvailable()) {
        return makeHyprlandWindowSource(events_socket);
    }
    if (swayAvailable()) {
        return makeSwayWindowSource(events_socket);
    }
    if (x11Available()) {
        if (auto source = makeX11WindowSource()) {
            return source;
        }
    }

    std::cerr << "[Window] No supported compositor detected; automatic profile switching is off.\n"
                 "         Drive it yourself with: sinoctl profile <name>\n";
    return nullptr;
}

}  // namespace kb::cfg
