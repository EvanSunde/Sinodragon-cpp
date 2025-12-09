#include "keyboard_configurator/config_loader.hpp"

// Use the system package or local include path
#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Required for keycode parsing
#include <libevdev/libevdev.h>

#include "keyboard_configurator/hidapi_transport.hpp"
#include "keyboard_configurator/logging_transport.hpp"

namespace kb::cfg {

namespace {

// --- Helper: String Trimming ---
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

// --- Helper: Bridge TOML values to String ---
std::string tomlToString(const toml::node& node) {
    if (auto val = node.as_string()) return val->get();
    if (auto val = node.as_integer()) return std::to_string(val->get());
    if (auto val = node.as_floating_point()) return std::to_string(val->get());
    if (auto val = node.as_boolean()) return val->get() ? "true" : "false";
    
    if (auto arr = node.as_array()) {
        std::ostringstream oss;
        bool first = true;
        for (const auto& elem : *arr) {
            if (!first) oss << ",";
            oss << tomlToString(elem);
            first = false;
        }
        return oss.str();
    }
    return "";
}

// --- Helper: Layout Parsing ---
KeyboardModel::Layout readLayout(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open layout file: " + path.string());
    }

    KeyboardModel::Layout layout;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        // Strip inline comments
        auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = trim(line.substr(0, hash_pos));
            if (line.empty()) continue;
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
    if (layout.empty()) throw std::runtime_error("Layout file is empty: " + path.string());
    return layout;
}

// --- Helper: Keycode Parsing ---
int parseKeycodeToken(const std::string& raw) {
    std::string token = trim(raw);
    if (token.empty()) return -1;
    std::string upper;
    upper.reserve(token.size());
    for (char ch : token) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    if (upper == "NAN" || upper == "NONE") return -1;
    if (upper.rfind("KEY_", 0) == 0 || upper.rfind("BTN_", 0) == 0) {
        int code = libevdev_event_code_from_name(EV_KEY, upper.c_str());
        if (code >= 0) return code;
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
    if (!in) throw std::runtime_error("Failed to open keycode file: " + path.string());

    std::vector<int> out;
    std::string line;
    std::size_t row_index = 0;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (row_index >= layout.size()) throw std::runtime_error("Keycode file too long");

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
    return out;
}

// --- Helper: Transport Factory ---
std::unique_ptr<DeviceTransport> createTransport(const std::string& id) {
    if (id == "logging") return std::make_unique<LoggingTransport>();
    if (id == "hidapi") return std::make_unique<HidapiTransport>();
    throw std::runtime_error("Unsupported transport: " + id);
}

// --- Helper: Parse Modifiers ---
int parseModifierMask(const std::string& key) {
    int mask = 0;
    std::string token;
    std::istringstream iss(key);
    while (std::getline(iss, token, '_')) {
        std::string t = trim(token);
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t == "ctrl" || t == "control") mask |= 1;
        else if (t == "shift") mask |= 2;
        else if (t == "alt") mask |= 4;
        else if (t == "super" || t == "win" || t == "meta") mask |= 8;
    }
    return mask;
}

} // namespace

ConfigLoader::ConfigLoader(const PresetRegistry& registry) : registry_(registry) {}

RuntimeConfig ConfigLoader::loadFromFile(const std::string& path) const {
    const auto file_path = std::filesystem::absolute(path);
    const auto root_dir = file_path.parent_path();

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        throw std::runtime_error("TOML Parse Error: " + std::string(err.description()));
    }

    auto device = tbl["device"];
    if (!device) throw std::runtime_error("Missing [device] section");

    std::string name = device["name"].value_or("Unknown Device");
    uint16_t vid = device["vendor_id"].value_or(0);
    uint16_t pid = device["product_id"].value_or(0);
    
    std::vector<uint8_t> header;
    if (auto arr = device["packet_header"].as_array()) {
        for (auto& byte : *arr) header.push_back(static_cast<uint8_t>(byte.value_or(0)));
    }

    size_t pkt_len = device["packet_length"].value_or(0);
    uint32_t fps = device["frame_interval_ms"].value_or(33);
    std::string transport = device["transport"].value_or("hidapi");
    
    std::filesystem::path layout_path = root_dir / device["layout"].value_or("");
    std::filesystem::path keycodes_path = root_dir / device["keycodes"].value_or("");

    // Load Helpers
    auto layout = readLayout(layout_path); 
    
    RuntimeConfig config{
        KeyboardModel(name, vid, pid, header, pkt_len, layout, std::nullopt, std::nullopt),
        createTransport(transport),
        {}, {},
        std::chrono::milliseconds(fps),
        std::nullopt, std::nullopt,
        {}, {}
    };

    if (std::filesystem::exists(keycodes_path)) {
         config.model.setKeycodeMap(readKeycodeCsv(keycodes_path, layout));
    }

    const std::size_t key_count = config.model.keyCount();

    // Load Zones
    std::unordered_map<std::string, std::vector<std::string>> zone_map;
    if (auto zones = tbl["zones"].as_table()) {
        for (auto& [key, val] : *zones) {
            if (auto arr = val.as_array()) {
                for (auto& k : *arr) zone_map[std::string(key.str())].push_back(k.value_or(""));
            }
        }
    }

    auto createPreset = [&](const std::string& type,
                            ParameterMap params) -> std::optional<std::size_t> {
        auto preset = registry_.create(type);
        if (!preset) {
            std::cerr << "Warning: Unknown preset type '" << type << "'.\n";
            return std::nullopt;
        }
        preset->configure(params);
        config.presets.push_back(std::move(preset));
        config.preset_parameters.push_back(std::move(params));
        config.preset_masks.emplace_back(key_count, true);
        config.preset_enabled.push_back(false);
        return config.presets.size() - 1;
    };

    // Load Hypr/Profiles
    if (auto hypr_node = tbl["hypr"]) {
        HyprConfig hcfg;
        hcfg.enabled = hypr_node["enabled"].value_or(false);
        
        auto growProfileMasks = [&](std::size_t idx) {
            for (auto& [_, masks] : hcfg.profile_masks) {
                if (masks.size() <= idx) {
                    masks.resize(idx + 1, std::vector<bool>(key_count, true));
                }
            }
        };

        hcfg.shortcuts_overlay_preset_index = -1;
        if (auto overlay_tbl = hypr_node["shortcuts_overlay_effect"].as_table()) {
            std::string overlay_type = "static_color";
            if (auto type_node = overlay_tbl->get("type")) {
                overlay_type = type_node->value_or("static_color");
            }
            ParameterMap overlay_params;
            for (auto& [k, v] : *overlay_tbl) {
                if (k.str() == "type") continue;
                overlay_params[std::string(k.str())] = tomlToString(v);
            }
            if (auto overlay_idx = createPreset(overlay_type, std::move(overlay_params))) {
                growProfileMasks(*overlay_idx);
                hcfg.shortcuts_overlay_preset_index = static_cast<int>(*overlay_idx);
            }
        }

        if (auto apps = tbl["apps"].as_table()) {
            hcfg.default_profile = (*apps)["default_profile"].value_or("default");
            hcfg.default_shortcut = (*apps)["default_shortcut"].value_or("default");
            if (auto maps = (*apps)["mappings"].as_table()) {
                 for (auto& [cls, prof] : *maps) {
                     // Check if it's a simple string or a table
                     if (auto prof_str = prof.as_string()) {
                        hcfg.class_to_profile[std::string(cls.str())] = prof_str->get();
                     } else if (auto prof_tbl = prof.as_table()) {
                         // Handle table format: "zen" = { shortcut = "zen" }
                         if (auto s = prof_tbl->get("shortcut")) {
                             hcfg.class_to_shortcut[std::string(cls.str())] = s->value_or("");
                         }
                         if (auto p = prof_tbl->get("profile")) {
                             hcfg.class_to_profile[std::string(cls.str())] = p->value_or("");
                         }
                     }
                 }
            }
        }

        // Load Profiles (Draw Order)
        if (auto profiles = tbl["profiles"].as_table()) {
            for (auto& [prof_name, prof_node] : *profiles) {
                std::string profile_id(prof_name.str());
                auto& draw_order = hcfg.profile_draw_order[profile_id];
                auto& profile_masks = hcfg.profile_masks[profile_id];

                auto applyZoneMask = [&](std::size_t target_idx, const toml::array& zones) {
                    for (auto& zn : zones) {
                        std::string zname = zn.value_or("");
                        auto zit = zone_map.find(zname);
                        if (zit == zone_map.end()) continue;
                        for (const auto& klabel : zit->second) {
                            if (auto idx = config.model.indexForKey(klabel)) {
                                profile_masks[target_idx][*idx] = true;
                            }
                        }
                    }
                };

                auto applyKeyMask = [&](std::size_t target_idx, const toml::array& keys) {
                    for (auto& kn : keys) {
                        if (auto idx = config.model.indexForKey(kn.value_or(""))) {
                            profile_masks[target_idx][*idx] = true;
                        }
                    }
                };

                auto parseInlineEffect = [&](const toml::table& layer_tbl)
                        -> std::optional<std::pair<std::string, ParameterMap>> {
                    const toml::table* effect_tbl = nullptr;
                    bool effect_is_layer = false;
                    if (auto effect_node = layer_tbl.get("effect")) {
                        effect_tbl = effect_node->as_table();
                    } else if (layer_tbl.contains("type")) {
                        effect_tbl = &layer_tbl;
                        effect_is_layer = true;
                    }
                    if (!effect_tbl) {
                        return std::nullopt;
                    }

                    std::string type = "static_color";
                    if (auto type_node = effect_tbl->get("type")) {
                        type = type_node->value_or("static_color");
                    }
                    ParameterMap params;
                    for (auto& [ekey, eval] : *effect_tbl) {
                        std::string key = std::string(ekey.str());
                        if (key == "type" || key == "name") continue;
                        if (effect_is_layer && (key == "zones" || key == "keys" || key == "effect")) {
                            continue;
                        }
                        params[key] = tomlToString(eval);
                    }
                    return std::make_pair(type, std::move(params));
                };

                if (auto layers_node = prof_node.as_table()->get("layers")) {
                    if (auto layers = layers_node->as_array()) {
                        for (auto& layer_node : *layers) {
                            const toml::table* layer_tbl = layer_node.as_table();
                            if (!layer_tbl) continue;

                            auto inline_effect = parseInlineEffect(*layer_tbl);
                            if (!inline_effect) {
                                std::cerr << "Warning: Profile '" << profile_id << "' has layer without effect definition.\n";
                                continue;
                            }

                            auto preset_idx_opt = createPreset(inline_effect->first, std::move(inline_effect->second));
                            if (!preset_idx_opt) {
                                continue;
                            }

                            std::size_t preset_idx = *preset_idx_opt;
                            growProfileMasks(preset_idx);
                            draw_order.push_back(preset_idx);

                            if (profile_masks.size() <= preset_idx) {
                                profile_masks.resize(preset_idx + 1, std::vector<bool>(key_count, true));
                            }

                            auto& mask = profile_masks[preset_idx];
                            mask.assign(key_count, true);

                            bool has_zones = layer_tbl->contains("zones");
                            bool has_keys = layer_tbl->contains("keys");

                            if (has_zones || has_keys) {
                                std::fill(mask.begin(), mask.end(), false);

                                if (has_zones) {
                                    if (auto zones = layer_tbl->get("zones")) {
                                        if (auto zarr = zones->as_array()) {
                                            applyZoneMask(preset_idx, *zarr);
                                        }
                                    }
                                }

                                if (has_keys) {
                                    if (auto keys = layer_tbl->get("keys")) {
                                        if (auto karr = keys->as_array()) {
                                            applyKeyMask(preset_idx, *karr);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Load Shortcuts Definitions
        if (auto shortcuts = tbl["shortcuts"].as_table()) {
            for (auto& [sc_name, sc_node] : *shortcuts) {
                std::string id(sc_name.str());
                ShortcutProfileConfig sc_cfg;
                
                if (auto tbl = sc_node.as_table()) {
                    sc_cfg.color = (*tbl)["color"].value_or("");
                    
                    for (auto& [mod_key, keys_node] : *tbl) {
                        std::string mod_str(mod_key.str());
                        if (mod_str == "color") continue;
                        
                        int mask = parseModifierMask(mod_str);
                        std::vector<std::string> key_list;
                        if (auto arr = keys_node.as_array()) {
                            for (auto& k : *arr) key_list.push_back(k.value_or(""));
                        }
                        sc_cfg.combos[mask] = key_list;
                    }
                }
                hcfg.shortcuts[id] = std::move(sc_cfg);
            }
        }

        config.hypr = std::move(hcfg);
    }

    return config;
}

} // namespace kb::cfg