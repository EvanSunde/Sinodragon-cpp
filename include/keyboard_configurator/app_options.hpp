#pragma once

#include <string>

namespace kb::cfg {

struct AppOptions {
    std::string config_path;
    std::string socket_path;
    std::string lock_path;
    bool daemon{false};
    bool preview{false};
    bool enable_socket{true};
    bool single_instance{true};
    bool show_help{false};
    bool show_version{false};
    bool valid{true};
    std::string error;
};

// Resolves the config file to use when none is given on the command line:
// $XDG_CONFIG_HOME/sinodragon/config.toml, then ~/.config/sinodragon/config.toml,
// then ./configs/config.toml so that running from a checkout still works.
[[nodiscard]] std::string defaultConfigPath();

[[nodiscard]] AppOptions parseArgs(int argc, char** argv);

[[nodiscard]] std::string usageText();

}  // namespace kb::cfg
