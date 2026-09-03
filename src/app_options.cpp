#include "keyboard_configurator/app_options.hpp"

#include <cstdlib>
#include <filesystem>

namespace kb::cfg {

namespace {

std::string envOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : std::string();
}

}  // namespace

std::string defaultConfigPath() {
    const std::string xdg = envOrEmpty("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        const auto candidate = std::filesystem::path(xdg) / "sinodragon" / "config.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }

    const std::string home = envOrEmpty("HOME");
    if (!home.empty()) {
        const auto candidate = std::filesystem::path(home) / ".config" / "sinodragon" / "config.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }

    return "configs/config.toml";
}

AppOptions parseArgs(int argc, char** argv) {
    AppOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-d" || arg == "--daemon") {
            options.daemon = true;
        } else if (arg == "-h" || arg == "--help") {
            options.show_help = true;
        } else if (arg == "-v" || arg == "--version") {
            options.show_version = true;
        } else if (arg == "-p" || arg == "--preview") {
            options.preview = true;
            // The preview redraws in place; sharing the terminal with the
            // interactive prompt would leave both garbled.
            options.daemon = true;
        } else if (arg == "--no-socket") {
            options.enable_socket = false;
        } else if (arg == "-s" || arg == "--socket") {
            if (i + 1 >= argc) {
                options.valid = false;
                options.error = arg + " needs a path";
                return options;
            }
            options.socket_path = argv[++i];
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                options.valid = false;
                options.error = arg + " needs a path";
                return options;
            }
            options.config_path = argv[++i];
        } else if (!arg.empty() && arg.front() == '-') {
            options.valid = false;
            options.error = "unknown option '" + arg + "'";
            return options;
        } else if (options.config_path.empty()) {
            // Bare positional path, the form the tool has always accepted.
            options.config_path = arg;
        } else {
            options.valid = false;
            options.error = "unexpected argument '" + arg + "'";
            return options;
        }
    }

    if (options.config_path.empty()) {
        options.config_path = defaultConfigPath();
    }
    return options;
}

std::string usageText() {
    return "Usage: sinodragon [options] [config.toml]\n"
           "\n"
           "Options:\n"
           "  -c, --config <path>   Config file to load\n"
           "  -d, --daemon          Run without the interactive prompt\n"
           "  -p, --preview         Draw frames in the terminal instead of\n"
           "                        sending them to the keyboard (implies --daemon)\n"
           "  -s, --socket <path>   Control socket to listen on\n"
           "                        (default: $XDG_RUNTIME_DIR/sinodragon.sock)\n"
           "      --no-socket       Do not listen for control commands\n"
           "  -h, --help            Show this help\n"
           "  -v, --version         Show the version\n"
           "\n"
           "With no config argument the daemon looks for\n"
           "  $XDG_CONFIG_HOME/sinodragon/config.toml\n"
           "  ~/.config/sinodragon/config.toml\n"
           "  ./configs/config.toml";
}

}  // namespace kb::cfg
