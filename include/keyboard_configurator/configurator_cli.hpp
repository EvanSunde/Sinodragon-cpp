#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class EffectEngine;
class KeyboardModel;
class ConfigWatcher;

class ConfiguratorCLI {
public:
    ConfiguratorCLI(const KeyboardModel& model,
                    EffectEngine& engine,
                    std::vector<ParameterMap> preset_parameters,
                    std::chrono::milliseconds frame_interval);
    ~ConfiguratorCLI();

    void run();

    // Watcher Interface (Public API)
    void setDrawList(const std::vector<std::size_t>& list);
    void applyPresetMasks(const std::vector<std::vector<bool>>& masks);
    void applyPresetMask(std::size_t index, const std::vector<bool>& mask);
    void applyPresetParameter(std::size_t index, const std::string& key, const std::string& value);
    void refreshRender();

    // Config Watch Interface
    void setConfigPath(const std::string& config_path);
    bool isConfigChanged() const;

    // REMOVED LEGACY METHODS:
    // void applyPresetEnable(std::size_t index, bool enabled);
    // void applyPresetEnableSet(const std::vector<bool>& enabled);
    // std::vector<bool> getPresetEnabledSet() const;

private:
    const KeyboardModel& model_;
    EffectEngine& engine_;
    
    // Protects engine_ access from CLI thread vs Render thread
    mutable std::mutex engine_mutex_;
    
    std::vector<ParameterMap> preset_parameters_;

    // Render Loop State
    std::atomic<bool> stop_flag_;
    std::atomic<int> frame_interval_ms_;
    std::atomic<bool> loop_running_;
    std::thread render_thread_;
    std::chrono::steady_clock::time_point start_time_;

    // Config Watch State
    std::unique_ptr<ConfigWatcher> config_watcher_;
    std::thread config_watch_thread_;
    std::atomic<bool> config_watch_enabled_;
    std::atomic<bool> config_changed_;
    std::mutex config_watch_mutex_;

    // Internal Helpers
    void printBanner() const;
    void printHelp() const;
    void printPresets();
    
    // Manual CLI Commands
    bool togglePreset(std::size_t index);
    bool setPresetParameter(std::size_t index, const std::string& key, const std::string& value);
    void handleSnakeCommand(const std::string& arg);
    void handleWatchCommand(const std::string& arg);

    // Config watch management
    void startConfigWatch();
    void stopConfigWatch();

    // Rendering logic
    bool engineHasAnimated() const;
    void renderOnce(double time_seconds);
    void startRenderLoop();
    void stopRenderLoop();
    void syncRenderState(bool refresh_static_frame);
    void applySnakeOverride(std::size_t snake_index);
    void clearSnakeOverride();

    // Drawlist/mask tracking for overrides
    std::vector<std::size_t> current_draw_list_;
    std::vector<std::size_t> saved_draw_list_;
    bool saved_draw_list_valid_ = false;

    std::vector<std::vector<bool>> current_masks_;
    std::vector<std::vector<bool>> saved_masks_;
    bool saved_masks_valid_ = false;

    bool snake_override_active_ = false;
};

}  // namespace kb::cfg