#include "keyboard_configurator/config_loader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <optional>

#include <libevdev/libevdev.h>

#include "keyboard_configurator/hidapi_transport.hpp"
#include "keyboard_configurator/logging_transport.hpp"

namespace kb::cfg {
namespace {

struct PresetSpec {
    std::string id;
    ParameterMap params;
};

std::string trim(const std::string& input) {
    auto begin = std::find_if_not(input.begin(), input.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    auto end = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::uint32_t parseNumber(const std::string& token) {
    int base = 10;
    std::size_t idx = 0;
    if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        base = 16;
        idx = 2;
    }
    auto value = std::stoul(token.substr(idx), nullptr, base);
    return static_cast<std::uint32_t>(value);
}

std::vector<std::uint8_t> parsePacketHeader(const std::string& line) {
    std::vector<std::uint8_t> header;
    std::string normalized = line;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream iss(normalized);
    std::string token;
    while (iss >> token) {
        header.push_back(static_cast<std::uint8_t>(parseNumber(token)));
    }
    if (header.empty()) {
        throw std::runtime_error("Packet header cannot be empty");
    }
    return header;
}

KeyboardModel::Layout readLayout(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open layout file: " + path.string());
    }

    KeyboardModel::Layout layout;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        // Strip inline comments after '#'
        {
            auto hash_pos = line.find('#');
            if (hash_pos != std::string::npos) {
                line = trim(line.substr(0, hash_pos));
                if (line.empty()) {
                    continue;
                }
            }
        }
        KeyboardModel::LayoutRow row;
        std::istringstream line_stream(line);
        std::string token;
        while (std::getline(line_stream, token, ',')) {
            token = trim(token);
            if (!token.empty()) {
                row.push_back(token);
            }
        }
        if (!row.empty()) {
            layout.push_back(std::move(row));
        }
    }

    if (layout.empty()) {
        throw std::runtime_error("Layout file is empty: " + path.string());
    }

    return layout;
}

int parseKeycodeToken(const std::string& raw) {
    std::string token = trim(raw);
    if (token.empty()) {
        return -1;
    }
    std::string upper;
    upper.reserve(token.size());
    for (char ch : token) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    if (upper == "NAN" || upper == "NONE") {
        return -1;
    }
    if (upper.rfind("KEY_", 0) == 0 || upper.rfind("BTN_", 0) == 0) {
        int code = libevdev_event_code_from_name(EV_KEY, upper.c_str());
        if (code >= 0) {
            return code;
        }
        throw std::runtime_error("Unknown keycode name: " + token);
    }
    try {
        return std::stoi(token);
    } catch (...) {
        throw std::runtime_error("Invalid keycode token: " + token);
    }
}

std::vector<int> readKeycodeCsv(const std::filesystem::path& path,
                                const KeyboardModel::Layout& layout) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open keycode file: " + path.string());
    }

    std::vector<int> out;
    std::string line;
    std::size_t row_index = 0;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (row_index >= layout.size()) {
            throw std::runtime_error("Keycode file has more rows than layout: " + path.string());
        }
        const auto& layout_row = layout[row_index];
        std::vector<int> row_codes;
        std::istringstream line_stream(line);
        std::string token;
        while (std::getline(line_stream, token, ',')) {
            row_codes.push_back(parseKeycodeToken(token));
        }
        if (row_codes.size() != layout_row.size()) {
            throw std::runtime_error("Keycode row size mismatch for row " + std::to_string(row_index));
        }
        out.insert(out.end(), row_codes.begin(), row_codes.end());
        ++row_index;
    }

    if (row_index != layout.size()) {
        throw std::runtime_error("Keycode file has fewer rows than layout: " + path.string());
    }

    return out;
}

std::unique_ptr<DeviceTransport> createTransport(const std::string& id) {
    if (id == "logging") {
        return std::make_unique<LoggingTransport>();
    } else if (id == "hidapi") {
        return std::make_unique<HidapiTransport>();
    }
    throw std::runtime_error("Unsupported transport: " + id);
}

std::vector<std::string> parseList(const std::string& value) {
    std::vector<std::string> items;
    std::string normalized = value;
    // replace semicolons with commas too
    std::replace(normalized.begin(), normalized.end(), ';', ',');
    std::istringstream iss(normalized);
    std::string token;
    while (std::getline(iss, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            items.push_back(token);
        }
    }
    return items;
}

bool parseBool(const std::string& value) {
    std::string v;
    v.reserve(value.size());
    for (char c : value) v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    // Fallback: non-empty treated as true
    return !v.empty();
}

std::vector<std::size_t> parseIndexList(const std::string& value) {
    std::vector<std::size_t> out;
    for (const auto& tok : parseList(value)) {
        try {
            out.push_back(static_cast<std::size_t>(parseNumber(tok)));
        } catch (...) {
        }
    }
    return out;
}

}  // namespace

ConfigLoader::ConfigLoader(const PresetRegistry& registry)
    : registry_(registry) {}

RuntimeConfig ConfigLoader::loadFromFile(const std::string& path) const {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    std::filesystem::path base_dir = std::filesystem::absolute(path).parent_path();

    std::string keyboard_name;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::vector<std::uint8_t> packet_header;
    std::size_t packet_length = 0;
    std::filesystem::path layout_path;
    std::filesystem::path keycodes_path;
    std::string transport_id;
    std::map<std::size_t, PresetSpec> presets_by_index;
    std::uint32_t frame_interval_ms = 33;
    std::optional<std::uint16_t> interface_usage_page;
    std::optional<std::uint16_t> interface_usage;

    // Zones and per-preset assignments
    std::unordered_map<std::string, std::vector<std::string>> zones;
    std::unordered_map<std::size_t, std::vector<std::string>> preset_keys;
    std::unordered_map<std::size_t, std::vector<std::string>> preset_zone_names;
    std::unordered_map<std::size_t, bool> preset_enabled_overrides;

    // Hyprland config
    bool hypr_enabled = false;
    std::string hypr_events_socket;
    // Section-driven profiles
    std::string current_section;
    std::string current_profile;
    std::string current_shortcuts;
    std::string default_profile_name;
    std::unordered_map<std::string, std::string> class_to_profile_temp;
    // Shortcuts temp storage
    int shortcuts_overlay_index = -1;
    std::string default_shortcut_name;
    std::unordered_map<std::string, std::string> class_to_shortcut_temp;
    std::unordered_map<std::string, ShortcutProfileConfig> shortcuts_temp;
    std::unordered_map<std::string, std::unordered_map<std::size_t, bool>> profile_enabled_raw;
    std::unordered_map<std::string, std::unordered_map<std::size_t, std::vector<std::string>>> profile_keys_raw;
    std::unordered_map<std::string, std::unordered_map<std::size_t, std::vector<std::string>>> profile_zones_raw;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Section header handling: [Section], [Profile:Name], [Shortcuts:Name]
        if (line.front() == '[' && line.back() == ']') {
            std::string sect = trim(line.substr(1, line.size() - 2));
            current_section.clear();
            current_profile.clear();
            current_shortcuts.clear();
            if (sect.rfind("Profile:", 0) == 0) {
                current_profile = trim(sect.substr(std::string("Profile:").size()));
            } else if (sect.rfind("Shortcuts:", 0) == 0) {
                current_shortcuts = trim(sect.substr(std::string("Shortcuts:").size()));
            } else {
                current_section = sect;
            }
            continue;
        }

        

        // Support '=', ':' and '(...)' assignment forms
        std::string key;
        std::string value;
        std::size_t sep_pos = line.find('=');
        if (sep_pos == std::string::npos) {
            sep_pos = line.find(':');
        }
        if (sep_pos != std::string::npos) {
            key = trim(line.substr(0, sep_pos));
            value = trim(line.substr(sep_pos + 1));
        } else {
            auto lp = line.find('(');
            auto rp = line.rfind(')');
            if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
                key = trim(line.substr(0, lp));
                value = trim(line.substr(lp + 1, rp - lp - 1));
            } else {
                throw std::runtime_error("Invalid line in config: " + std::to_string(line_number));
            }
        }

        // [Presets] explicit index form: preset.<index> = <id> [k=v...]
        if (current_section == "Presets") {
            if (key.rfind("preset.", 0) == 0) {
                auto idxstr = key.substr(std::string("preset.").size());
                if (idxstr.find('.') == std::string::npos) {
                    std::size_t pindex = static_cast<std::size_t>(parseNumber(idxstr));
                    std::istringstream iss(value);
                    PresetSpec spec;
                    if (!(iss >> spec.id)) {
                        throw std::runtime_error("Missing preset identifier on line " + std::to_string(line_number));
                    }
                    std::string token;
                    while (iss >> token) {
                        auto eq = token.find('=');
                        if (eq == std::string::npos) {
                            throw std::runtime_error("Invalid preset parameter on line " + std::to_string(line_number));
                        }
                        auto pkey = token.substr(0, eq);
                        auto pval = token.substr(eq + 1);
                        spec.params.emplace(std::move(pkey), std::move(pval));
                    }
                    presets_by_index[pindex] = std::move(spec);
                    continue;
                }
            }
        }

        // [ApplicationProfiles] mapping
        if (current_section == "ApplicationProfiles") {
            auto lower = key;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto strip_quotes = [](const std::string& s) {
                if (s.size() >= 2 && ((s.front()=='"' && s.back()=='"') || (s.front()=='\'' && s.back()=='\''))) {
                    return trim(s.substr(1, s.size()-2));
                }
                return trim(s);
            };
            // RHS may contain comma-separated tokens, e.g. "Profile:Name, shortcut:Name"
            auto parse_assignments = [&](const std::string& rhs, std::string& outProfile, std::string& outShortcut) {
                std::string v = rhs;
                // split by commas
                std::vector<std::string> parts;
                std::string tmp;
                std::istringstream pss(v);
                while (std::getline(pss, tmp, ',')) {
                    tmp = trim(tmp);
                    if (!tmp.empty()) parts.push_back(tmp);
                }
                if (parts.empty()) parts.push_back(v);
                for (const auto& p : parts) {
                    if (p.rfind("Profile:", 0) == 0) {
                        outProfile = trim(p.substr(std::string("Profile:").size()));
                    } else if (p.rfind("shortcut:", 0) == 0 || p.rfind("Shortcut:", 0) == 0) {
                        std::size_t off = (p[0] == 's' || p[0] == 'S') ? std::string("shortcut:").size() : std::string("Shortcut:").size();
                        outShortcut = trim(p.substr(off));
                    }
                }
            };
            // Save mapping class->profile and optional shortcut
            std::string klass = strip_quotes(key);
            std::string profname;
            std::string shortcutname;
            parse_assignments(value, profname, shortcutname);
            hypr_enabled = true;
            if (lower == "default") {
                if (!profname.empty()) default_profile_name = profname;
                if (!shortcutname.empty()) default_shortcut_name = shortcutname;
                continue;
            } else {
                if (!profname.empty()) class_to_profile_temp[klass] = profname;
                if (!shortcutname.empty()) class_to_shortcut_temp[klass] = shortcutname;
                continue;
            }
        }

        if (key == "keyboard.name") {
            keyboard_name = value;
        } else if (key == "keyboard.vendor_id") {
            vendor_id = static_cast<std::uint16_t>(parseNumber(value));
        } else if (key == "keyboard.product_id") {
            product_id = static_cast<std::uint16_t>(parseNumber(value));
        } else if (key == "keyboard.packet_header") {
            packet_header = parsePacketHeader(value);
        } else if (key == "keyboard.packet_length") {
            packet_length = static_cast<std::size_t>(parseNumber(value));
        } else if (key == "keyboard.layout") {
            layout_path = base_dir / value;
        } else if (key == "keyboard.keycodes") {
            keycodes_path = base_dir / value;
        } else if (key == "transport") {
            transport_id = value;
        } else if (key == "engine.frame_interval_ms") {
            frame_interval_ms = static_cast<std::uint32_t>(parseNumber(value));
        } else if (key == "keyboard.interface_usage_page") {
            interface_usage_page = static_cast<std::uint16_t>(parseNumber(value));
        } else if (key == "keyboard.interface_usage") {
            interface_usage = static_cast<std::uint16_t>(parseNumber(value));
        } else if (key.rfind("zone.", 0) == 0) {
            // zone.<name> = key1,key2,...
            std::string zone_name = key.substr(std::string("zone.").size());
            zones[zone_name] = parseList(value);
        } else if (key == "hypr.enabled" || key == "hypr.enable") {
            hypr_enabled = parseBool(value);
        } else if (key == "hypr.events_socket") {
            hypr_events_socket = value;
        } else if (key.rfind("preset.", 0) == 0) {
            // preset.<index>.<field>
            auto rest = key.substr(std::string("preset.").size());
            auto dot = rest.find('.');
            if (dot == std::string::npos) {
                throw std::runtime_error("Invalid preset.* key on line " + std::to_string(line_number));
            }
            auto index_str = trim(rest.substr(0, dot));
            auto field = trim(rest.substr(dot + 1));
            std::size_t pindex = static_cast<std::size_t>(parseNumber(index_str));
            if (!current_profile.empty()) {
                if (field == "keys") {
                    profile_keys_raw[current_profile][pindex] = parseList(value);
                } else if (field == "zones") {
                    profile_zones_raw[current_profile][pindex] = parseList(value);
                } else if (field == "enabled") {
                    profile_enabled_raw[current_profile][pindex] = parseBool(value);
                } else {
                    throw std::runtime_error("Unknown configuration key: " + key);
                }
            } else {
                if (field == "keys") {
                    preset_keys[pindex] = parseList(value);
                } else if (field == "zones") {
                    preset_zone_names[pindex] = parseList(value);
                } else if (field == "enabled") {
                    preset_enabled_overrides[pindex] = parseBool(value);
                } else {
                    throw std::runtime_error("Unknown configuration key: " + key);
                }
            }
        } else if (key == "shortcuts.overlay_preset_index") {
            shortcuts_overlay_index = static_cast<int>(parseNumber(value));
        } else if (!current_shortcuts.empty()) {
            // Inside [Shortcuts:<Name>]
            auto& scfg = shortcuts_temp[current_shortcuts];
            if (key == "color") {
                scfg.color = value;
            } else {
                // key like ctrl, ctrl_shift, alt, super, alt_shift, etc.
                auto parse_mods = [](const std::string& s) -> int {
                    int mask = 0;
                    std::string token;
                    std::istringstream iss(s);
                    while (std::getline(iss, token, '_')) {
                        std::string t;
                        t.reserve(token.size());
                        for (char c : token) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                        if (t == "ctrl" || t == "control") mask |= 1;
                        else if (t == "shift") mask |= 2;
                        else if (t == "alt") mask |= 4;
                        else if (t == "super" || t == "win" || t == "meta" || t == "windows") mask |= 8;
                    }
                    return mask;
                };
                int m = parse_mods(key);
                if (m > 0) {
                    scfg.combos[m] = parseList(value);
                } else {
                    throw std::runtime_error("Unknown shortcuts key: " + key);
                }
            }
        } else {
            throw std::runtime_error("Unknown configuration key: " + key);
        }
    }

    if (keyboard_name.empty()) {
        throw std::runtime_error("keyboard.name must be provided");
    }
    if (vendor_id == 0 || product_id == 0) {
        throw std::runtime_error("Vendor/Product IDs must be provided");
    }
    if (packet_header.empty()) {
        throw std::runtime_error("keyboard.packet_header must be provided");
    }
    if (packet_length == 0) {
        throw std::runtime_error("keyboard.packet_length must be provided");
    }
    if (layout_path.empty()) {
        throw std::runtime_error("keyboard.layout must be provided");
    }
    if (transport_id.empty()) {
        throw std::runtime_error("transport must be provided");
    }

    auto layout = readLayout(layout_path);
    std::vector<int> keycodes;
    bool has_keycodes = false;
    if (!keycodes_path.empty()) {
        keycodes = readKeycodeCsv(keycodes_path, layout);
        has_keycodes = true;
    }

    const auto frame_interval = std::chrono::milliseconds(frame_interval_ms == 0 ? 1 : frame_interval_ms);

    RuntimeConfig runtime_config{
        KeyboardModel(
            keyboard_name,
            vendor_id,
            product_id,
            packet_header,
            packet_length,
            std::move(layout),
            interface_usage_page,
            interface_usage),
        nullptr,
        {},
        {},
        frame_interval,
        interface_usage_page,
        interface_usage,
        {},
        {}
    };

    runtime_config.transport = createTransport(transport_id);
    if (!runtime_config.transport) {
        throw std::runtime_error("Transport creation failed");
    }

    if (has_keycodes) {
        runtime_config.model.setKeycodeMap(keycodes);
    }

    if (!presets_by_index.empty()) {
        for (const auto& kv : presets_by_index) {
            const auto& spec = kv.second;
            auto preset = registry_.create(spec.id);
            preset->configure(spec.params);
            runtime_config.presets.push_back(std::move(preset));
            runtime_config.preset_parameters.push_back(spec.params);
        }
    }

    if (runtime_config.presets.empty()) {
        auto default_preset = registry_.create("static_color");
        runtime_config.presets.push_back(std::move(default_preset));
        runtime_config.preset_parameters.emplace_back();
    }

    // Build masks and enabled flags
    const auto kc = runtime_config.model.keyCount();
    const auto pc = runtime_config.presets.size();
    runtime_config.preset_masks.assign(pc, std::vector<bool>(kc, true));
    runtime_config.preset_enabled.assign(pc, false);
    if (pc > 0) runtime_config.preset_enabled[0] = true;

    auto applyKeysToMask = [&](std::size_t pidx, const std::vector<std::string>& keys){
        auto& mask = runtime_config.preset_masks[pidx];
        std::fill(mask.begin(), mask.end(), false);
        for (const auto& label : keys) {
            if (auto opt = runtime_config.model.indexForKey(label)) {
                mask[*opt] = true;
            }
        }
    };

    for (const auto& kv : preset_keys) {
        if (kv.first < pc) {
            applyKeysToMask(kv.first, kv.second);
        }
    }
    for (const auto& kv : preset_zone_names) {
        if (kv.first < pc) {
            auto it = kv.second.begin();
            // If zones specified, start from empty mask
            auto& mask = runtime_config.preset_masks[kv.first];
            std::fill(mask.begin(), mask.end(), false);
            for (; it != kv.second.end(); ++it) {
                auto zit = zones.find(*it);
                if (zit != zones.end()) {
                    for (const auto& label : zit->second) {
                        if (auto opt = runtime_config.model.indexForKey(label)) {
                            mask[*opt] = true;
                        }
                    }
                }
            }
        }
    }
    for (const auto& kv : preset_enabled_overrides) {
        if (kv.first < pc) {
            runtime_config.preset_enabled[kv.first] = kv.second;
        }
    }

    // Hypr config
    if (hypr_enabled) {
        HyprConfig hcfg;
        hcfg.enabled = true;
        hcfg.events_socket = std::move(hypr_events_socket);
        hcfg.default_profile = std::move(default_profile_name);
        hcfg.class_to_profile = std::move(class_to_profile_temp);
        // Shortcuts
        hcfg.shortcuts_overlay_preset_index = shortcuts_overlay_index;
        hcfg.default_shortcut = std::move(default_shortcut_name);
        hcfg.class_to_shortcut = std::move(class_to_shortcut_temp);
        hcfg.shortcuts = std::move(shortcuts_temp);
        // Compile profile-based mappings if any
        // Build from profile_*_raw maps
        const auto pc2 = runtime_config.presets.size();
        // Gather profile names
        std::unordered_map<std::string, bool> profile_names;
        for (const auto& kv : profile_enabled_raw) profile_names[kv.first] = true;
        for (const auto& kv : profile_keys_raw) profile_names[kv.first] = true;
        for (const auto& kv : profile_zones_raw) profile_names[kv.first] = true;
        if (!hcfg.default_profile.empty()) profile_names[hcfg.default_profile] = true;
        for (const auto& kv : hcfg.class_to_profile) profile_names[kv.second] = true;
        for (const auto& it : profile_names) {
            const std::string& pname = it.first;
            // start with global masks as defaults
            std::vector<std::vector<bool>> masks = runtime_config.preset_masks;
            // enabled default: all false
            std::vector<bool> pen(pc2, false);
            // apply per-preset keys
            if (auto it2 = profile_keys_raw.find(pname); it2 != profile_keys_raw.end()) {
                for (const auto& kv2 : it2->second) {
                    if (kv2.first < pc2) {
                        auto& m = masks[kv2.first];
                        std::fill(m.begin(), m.end(), false);
                        for (const auto& label : kv2.second) {
                            if (auto opt = runtime_config.model.indexForKey(label)) m[*opt] = true;
                        }
                    }
                }
            }
            // apply per-preset zones
            if (auto it3 = profile_zones_raw.find(pname); it3 != profile_zones_raw.end()) {
                for (const auto& kv3 : it3->second) {
                    if (kv3.first < pc2) {
                        auto& m = masks[kv3.first];
                        std::fill(m.begin(), m.end(), false);
                        for (const auto& zname : kv3.second) {
                            auto zit = zones.find(zname);
                            if (zit != zones.end()) {
                                for (const auto& label : zit->second) {
                                    if (auto opt = runtime_config.model.indexForKey(label)) m[*opt] = true;
                                }
                            }
                        }
                    }
                }
            }
            // enabled overrides in profile
            if (auto it4 = profile_enabled_raw.find(pname); it4 != profile_enabled_raw.end()) {
                for (const auto& kv4 : it4->second) {
                    if (kv4.first < pc2) pen[kv4.first] = kv4.second;
                }
            }
            hcfg.profile_masks.emplace(pname, std::move(masks));
            hcfg.profile_enabled.emplace(pname, std::move(pen));
        }
        runtime_config.hypr = std::move(hcfg);
    }

    return runtime_config;
}

}  // namespace kb::cfg
