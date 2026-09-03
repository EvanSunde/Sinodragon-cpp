#pragma once

#include <string>

#include "keyboard_configurator/key_activity.hpp"
#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/system_state.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class LightingPreset {
public:
    virtual ~LightingPreset() = default;

    virtual std::string id() const = 0;
    virtual void configure(const ParameterMap& params) { (void)params; }
    virtual void render(const KeyboardModel& model,
                        double time_seconds,
                        KeyColorFrame& frame) = 0;
    [[nodiscard]] virtual bool isAnimated() const noexcept { return false; }
    virtual void setKeyActivityProvider(KeyActivityProviderPtr provider) {
        (void)provider;
    }

    // Only the data-driven presets care; the rest ignore it.
    virtual void setSystemState(SystemStatePtr state) { (void)state; }
};

}  // namespace kb::cfg
