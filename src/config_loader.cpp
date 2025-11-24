#include "keyboard_configurator/config_loader.hpp"

// Use the system package include path
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

// --- Helper: Layout Parsing (Restored) ---
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

// --- Helper: Keycode Parsing (Restored) ---
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

std::unique_ptr<DeviceTransport> createTransport(const std::string& id) {
    if (id == "logging") return std::make_unique<LoggingTransport>();
    if (id == "hidapi") return std::make_unique<HidapiTransport>();
    throw std::runtime_error("Unsupported transport: " + id);
}

} // namespace

ConfigLoader::ConfigLoader(const PresetRegistry& registry) : registry_(registry) {}

RuntimeConfig ConfigLoader::loadFromFile(const std::string& path) const {
    const auto file_path = std::filesystem::absolute(path);
    const auto root_dir = file_path.parent_path();

    // 1. Parse TOML
    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        throw std::runtime_error("TOML Parse Error: " + std::string(err.description()));
    }

    // 2. Load Device Settings
    auto device = tbl["device"];
    if (!device) throw std::runtime_error("Missing [device] section");

    std::string name = device["name"].value_or("Unknown Device");
    uint16_t vid = device["vendor_id"].value_or(0);
    uint16_t pid = device["product_id"].value_or(0);
    
    // Handle Header Array
    std::vector<uint8_t> header;
    if (auto arr = device["packet_header"].as_array()) {
        for (auto& byte : *arr) header.push_back(static_cast<uint8_t>(byte.value_or(0)));
    }

    size_t pkt_len = device["packet_length"].value_or(0);
    uint32_t fps = device["frame_interval_ms"].value_or(33);
    std::string transport = device["transport"].value_or("hidapi");
    
    std::filesystem::path layout_path = root_dir / device["layout"].value_or("");
    std::filesystem::path keycodes_path = root_dir / device["keycodes"].value_or("");

    // 3. Construct RuntimeConfig (Device Model)
    auto layout = readLayout(layout_path); 
    
    RuntimeConfig config{
        KeyboardModel(name, vid, pid, header, pkt_len, layout, std::nullopt, std::nullopt),
        createTransport(transport),
        {}, {}, // presets, params
        std::chrono::milliseconds(fps),
        std::nullopt, std::nullopt,
        {}, {}  // masks, enabled
    };

    if (std::filesystem::exists(keycodes_path)) {
         config.model.setKeycodeMap(readKeycodeCsv(keycodes_path, layout));
    }

    // 4. Load Zones (Global)
    std::unordered_map<std::string, std::vector<std::string>> zone_map;
    if (auto zones = tbl["zones"].as_table()) {
        for (auto& [key, val] : *zones) {
            if (auto arr = val.as_array()) {
                for (auto& k : *arr) zone_map[std::string(key.str())].push_back(k.value_or(""));
            }
        }
    }

    // 5. Load Presets
    // We need to map TOML "names" to Vector Indices for profiles later
    std::map<std::string, size_t> preset_name_to_index;
    
    if (auto presets = tbl["presets"].as_table()) {
        for (auto& [name, node] : *presets) {
            if (auto ptable = node.as_table()) {
                std::string id_str(name.str());
                std::string type = (*ptable)["type"].value_or("static_color");
                
                // Create Param Map from TOML Table
                ParameterMap params;
                for (auto& [pkey, pval] : *ptable) {
                    if (pkey.str() == "type") continue; // Don't pass 'type' as param
                    params[std::string(pkey.str())] = tomlToString(pval);
                }

                // Instantiate
                auto preset = registry_.create(type);
                if (preset) {
                    preset->configure(params);
                    config.presets.push_back(std::move(preset));
                    config.preset_parameters.push_back(params);
                    
                    // Map Name -> Index
                    preset_name_to_index[id_str] = config.presets.size() - 1;
                }
            }
        }
    }

    // Init global masks (Default: All Enabled)
    size_t kc = config.model.keyCount();
    size_t pc = config.presets.size();
    config.preset_masks.assign(pc, std::vector<bool>(kc, true));
    config.preset_enabled.assign(pc, false); // Default off until profile enables them

    // 6. Load Hypr/Profiles
    if (auto profiles = tbl["profiles"].as_table()) {
        HyprConfig hcfg;
        hcfg.enabled = true;
        
        // Default Configs
        if (auto apps = tbl["apps"].as_table()) {
            hcfg.default_profile = (*apps)["default_profile"].value_or("default");
            if (auto maps = (*apps)["mappings"].as_table()) {
                 for (auto& [cls, prof] : *maps) {
                     hcfg.class_to_profile[std::string(cls.str())] = prof.value_or("");
                 }
            }
        }

        // Process Each Profile
        for (auto& [prof_name, prof_node] : *profiles) {
            std::string profile_id(prof_name.str());
            
            // Start with blank state for this profile
            std::vector<std::vector<bool>> profile_masks(pc, std::vector<bool>(kc, true));
            std::vector<bool> profile_enabled(pc, false);

            if (auto layers = prof_node.as_table()->get("layers")->as_array()) {
                for (auto& layer_node : *layers) {
                    // Which preset is this?
                    std::string p_ref = layer_node.as_table()->get("preset")->value_or("");
                    if (preset_name_to_index.find(p_ref) == preset_name_to_index.end()) continue;
                    
                    size_t p_idx = preset_name_to_index[p_ref];
                    profile_enabled[p_idx] = true;

                    // Handle Zone/Key overrides
                    bool has_override = false;
                    std::vector<bool> mask(kc, false); // Default empty if overriding

                    // Zones
                    if (auto z = layer_node.as_table()->get("zones")) {
                        has_override = true;
                        if (auto zarr = z->as_array()) {
                            for (auto& zn : *zarr) {
                                std::string zname = zn.value_or("");
                                if (zone_map.count(zname)) {
                                    for(const auto& klabel : zone_map[zname]) {
                                         if(auto idx = config.model.indexForKey(klabel)) mask[*idx] = true;
                                    }
                                }
                            }
                        }
                    }
                    
                    // Keys
                    if (auto k = layer_node.as_table()->get("keys")) {
                        has_override = true;
                         if (auto karr = k->as_array()) {
                            for (auto& kn : *karr) {
                                if(auto idx = config.model.indexForKey(kn.value_or(""))) mask[*idx] = true;
                            }
                        }
                    }

                    if (has_override) {
                        profile_masks[p_idx] = mask;
                    }
                }
            }
            hcfg.profile_masks[profile_id] = profile_masks;
            hcfg.profile_enabled[profile_id] = profile_enabled;
        }
        config.hypr = std::move(hcfg);
    }

    return config;
}

} // namespace kb::cfg