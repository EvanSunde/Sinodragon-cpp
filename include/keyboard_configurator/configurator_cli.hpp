#pragma once

#include <string>

namespace kb::cfg {

class EffectEngine;
class KeyboardModel;

class ConfiguratorCLI {
public:
    ConfiguratorCLI(const KeyboardModel& model, EffectEngine& engine);

    void run();

private:
    const KeyboardModel& model_;
    EffectEngine& engine_;
};

}  // namespace kb::cfg
