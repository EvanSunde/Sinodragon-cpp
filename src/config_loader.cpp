#include "keyboard_configurator/config_loader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <optional>

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
    std::string transport_id;
    std::vector<PresetSpec> preset_specs;
    std::uint32_t frame_interval_ms = 33;
    std::optional<std::uint16_t> interface_usage_page;
    std::optional<std::uint16_t> interface_usage;

    // Zones and per-preset assignments
    std::unordered_map<std::string, std::vector<std::string>> zones;
    std::unordered_map<std::size_t, std::vector<std::string>> preset_keys;
    std::unordered_map<std::size_t, std::vector<std::string>> preset_zone_names;
    std::unordered_map<std::size_t, bool> preset_enabled_overrides;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Only treat lines starting with 'preset' followed by whitespace or '=' as preset declarations.
        if (line.rfind("preset", 0) == 0 && (line.size() == 6 || std::isspace(static_cast<unsigned char>(line[6])) || line[6] == '=')) {
            std::string remainder = line.substr(std::string("preset").size());
            remainder = trim(remainder);
            if (remainder.empty() || remainder[0] != '=') {
                throw std::runtime_error("Invalid preset declaration on line " + std::to_string(line_number));
            }
            remainder.erase(0, 1);  // remove '='
            remainder = trim(remainder);

            std::istringstream iss(remainder);
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
                auto key = token.substr(0, eq);
                auto value = token.substr(eq + 1);
                spec.params.emplace(std::move(key), std::move(value));
            }
            preset_specs.push_back(std::move(spec));
            continue;
        }

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            throw std::runtime_error("Invalid line in config: " + std::to_string(line_number));
        }
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

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
            if (field == "keys") {
                preset_keys[pindex] = parseList(value);
            } else if (field == "zones") {
                preset_zone_names[pindex] = parseList(value);
            } else if (field == "enabled") {
                preset_enabled_overrides[pindex] = parseBool(value);
            } else {
                throw std::runtime_error("Unknown configuration key: " + key);
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

    for (auto& spec : preset_specs) {
        auto preset = registry_.create(spec.id);
        preset->configure(spec.params);
        runtime_config.presets.push_back(std::move(preset));
        runtime_config.preset_parameters.push_back(std::move(spec.params));
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

    return runtime_config;
}

}  // namespace kb::cfg
