#include "keyboard_configurator/config_loader.hpp"

// Use the system package or local include path
#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

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

    // Load Zones
    std::unordered_map<std::string, std::vector<std::string>> zone_map;
    if (auto zones = tbl["zones"].as_table()) {
        for (auto& [key, val] : *zones) {
            if (auto arr = val.as_array()) {
                for (auto& k : *arr) zone_map[std::string(key.str())].push_back(k.value_or(""));
            }
        }
    }

    // Load Presets
    std::map<std::string, size_t> preset_name_to_index;
    if (auto presets = tbl["presets"].as_table()) {
        for (auto& [name, node] : *presets) {
            if (auto ptable = node.as_table()) {
                std::string id_str(name.str());
                std::string type = (*ptable)["type"].value_or("static_color");
                ParameterMap params;
                for (auto& [pkey, pval] : *ptable) {
                    if (pkey.str() == "type") continue; 
                    params[std::string(pkey.str())] = tomlToString(pval);
                }
                auto preset = registry_.create(type);
                if (preset) {
                    preset->configure(params);
                    config.presets.push_back(std::move(preset));
                    config.preset_parameters.push_back(params);
                    preset_name_to_index[id_str] = config.presets.size() - 1;
                }
            }
        }
    }

    // Init Global Masks
    size_t kc = config.model.keyCount();
    size_t pc = config.presets.size();
    config.preset_masks.assign(pc, std::vector<bool>(kc, true));
    config.preset_enabled.assign(pc, false);

    // Load Hypr/Profiles
    if (auto hypr_node = tbl["hypr"]) {
        HyprConfig hcfg;
        hcfg.enabled = hypr_node["enabled"].value_or(false);
        
        // Resolve Overlay Name to Index
        std::string overlay_name = hypr_node["shortcuts_overlay_preset"].value_or("");
        if (!overlay_name.empty() && preset_name_to_index.count(overlay_name)) {
            hcfg.shortcuts_overlay_preset_index = static_cast<int>(preset_name_to_index[overlay_name]);
        } else {
            if (!overlay_name.empty()) std::cerr << "Warning: Shortcut overlay preset '" << overlay_name << "' not found.\n";
            hcfg.shortcuts_overlay_preset_index = -1;
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
                std::vector<std::size_t> draw_order;
                std::vector<std::vector<bool>> masks(pc, std::vector<bool>(kc, true));

                if (auto layers = prof_node.as_table()->get("layers")->as_array()) {
                    for (auto& layer_node : *layers) {
                        std::string p_ref = layer_node.as_table()->get("preset")->value_or("");
                        if (preset_name_to_index.find(p_ref) == preset_name_to_index.end()) continue;
                        
                        size_t p_idx = preset_name_to_index[p_ref];
                        draw_order.push_back(p_idx);

                        // Handle Mask Overrides
                        if (layer_node.as_table()->contains("zones") || layer_node.as_table()->contains("keys")) {
                            std::fill(masks[p_idx].begin(), masks[p_idx].end(), false); // Reset mask to empty
                            
                            if (auto z = layer_node.as_table()->get("zones")) {
                                if (auto zarr = z->as_array()) {
                                    for (auto& zn : *zarr) {
                                        std::string zname = zn.value_or("");
                                        if (zone_map.count(zname)) {
                                            for(const auto& klabel : zone_map[zname]) {
                                                 if(auto idx = config.model.indexForKey(klabel)) masks[p_idx][*idx] = true;
                                            }
                                        }
                                    }
                                }
                            }
                            if (auto k = layer_node.as_table()->get("keys")) {
                                 if (auto karr = k->as_array()) {
                                    for (auto& kn : *karr) {
                                        if(auto idx = config.model.indexForKey(kn.value_or(""))) masks[p_idx][*idx] = true;
                                    }
                                }
                            }
                        }
                    }
                }
                hcfg.profile_draw_order[profile_id] = draw_order;
                hcfg.profile_masks[profile_id] = masks;
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