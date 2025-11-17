#pragma once

#include <string>
#include <vector>

#include <mutex>

#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class EffectEngine;
class KeyboardModel;

class ConfiguratorCLI {
public:
    ConfiguratorCLI(const KeyboardModel& model,
                    EffectEngine& engine,
                    std::vector<ParameterMap> preset_parameters);

    void run();

private:
    const KeyboardModel& model_;
    EffectEngine& engine_;
    std::vector<ParameterMap> preset_parameters_;
    mutable std::mutex engine_mutex_;

    void printBanner() const;
    void printHelp() const;
    void printPresets();
    bool togglePreset(std::size_t index);
    bool setPresetParameter(std::size_t index, const std::string& key, const std::string& value);
};

}  // namespace kb::cfg
