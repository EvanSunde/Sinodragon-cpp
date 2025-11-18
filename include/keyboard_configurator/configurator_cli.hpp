#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class EffectEngine;
class KeyboardModel;

class ConfiguratorCLI {
public:
    ConfiguratorCLI(const KeyboardModel& model,
                    EffectEngine& engine,
                    std::vector<ParameterMap> preset_parameters,
                    std::chrono::milliseconds frame_interval);
    ~ConfiguratorCLI();

    void run();
    
    // Thread-safe control from external integrations (e.g., Hyprland watcher)
    void applyPresetEnable(std::size_t index, bool enabled);
    void applyPresetEnableSet(const std::vector<bool>& enabled);
    void applyPresetMasks(const std::vector<std::vector<bool>>& masks);
    void applyPresetMask(std::size_t index, const std::vector<bool>& mask);
    void applyPresetParameter(std::size_t index, const std::string& key, const std::string& value);
    void refreshRender();

private:
    const KeyboardModel& model_;
    EffectEngine& engine_;
    std::vector<ParameterMap> preset_parameters_;
    mutable std::mutex engine_mutex_;
    std::atomic<bool> stop_flag_;
    std::thread render_thread_;
    std::atomic<bool> loop_running_;
    std::atomic<int> frame_interval_ms_;
    std::chrono::steady_clock::time_point start_time_;

    void printBanner() const;
    void printHelp() const;
    void printPresets();
    bool togglePreset(std::size_t index);
    bool setPresetParameter(std::size_t index, const std::string& key, const std::string& value);
    bool engineHasAnimated() const;
    void renderOnce(double time_seconds);
    void startRenderLoop();
    void stopRenderLoop();
    void syncRenderState(bool refresh_static_frame);
};

}  // namespace kb::cfg
