#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "keyboard_configurator/config_loader.hpp"
#include "keyboard_configurator/effect_engine.hpp"
#include "keyboard_configurator/key_activity.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/system_state.hpp"

namespace kb::cfg {

// Owns the device, the engine and the one render thread, and is the single
// place commands are dispatched from. Every frontend -- the interactive CLI,
// the control socket, the window watchers -- drives the daemon through this
// object, so there is exactly one lock protecting the engine and exactly one
// thread touching the device.
//
// Locking rules, in order:
//   1. loop_mutex_ may be taken before engine_mutex_ (the render thread does).
//   2. engine_mutex_ must never be held while taking loop_mutex_.
// wake() takes no lock at all, so callers holding engine_mutex_ can never
// deadlock against the render thread.
class ConfigLoader;

class Runtime {
public:
    Runtime(RuntimeConfig config, std::string config_path, const ConfigLoader& loader);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Opens the device with exponential backoff. False means every attempt failed.
    bool connect();

    // Starts the render thread and applies the default profile so the keyboard
    // lights up immediately instead of waiting for the first window event.
    void start();
    void stop();

    // Pushes a single all-black frame. Used on shutdown so the keyboard does not
    // keep displaying whatever was on it when the daemon died.
    void blank();

    // Runs one command line and returns the reply. Safe from any thread.
    std::string execute(const std::string& line);

    [[nodiscard]] const KeyboardModel& model() const noexcept { return model_; }
    [[nodiscard]] KeyActivityProviderPtr keyActivity() const noexcept { return key_activity_; }
    [[nodiscard]] SystemStatePtr systemState() const noexcept { return system_state_; }
    // Returns a copy, not a reference: reload() replaces hypr_ under the
    // engine lock, so handing out a reference would let a caller read it
    // while the config watcher is rewriting it.
    [[nodiscard]] std::optional<HyprConfig> hyprConfig() const;
    [[nodiscard]] std::size_t presetCount() const;
    [[nodiscard]] bool shouldQuit() const noexcept { return quit_requested_.load(); }
    [[nodiscard]] bool configChanged() const noexcept { return config_changed_.load(); }
    [[nodiscard]] const std::string& configPath() const noexcept { return config_path_; }

    void requestQuit();

    // Reloads the config in place: presets, masks, styles, profiles, shortcuts
    // and brightness are all swapped without dropping the device handle or
    // restarting the watchers. Returns a human-readable result. If the [device]
    // section changed in a way that needs a new handle, this flags a restart
    // instead and configChanged() becomes true.
    std::string reload();

    // Notified after a successful reload so long-lived watchers can pick up the
    // new shortcut tables rather than holding a stale copy.
    void setConfigObserver(std::function<void(const HyprConfig&)> observer);

    // --- Applied by the window and shortcut watchers ---
    // Resolves a focused window to its profile using the *current* config, so
    // a reload takes effect on the next window switch with no other plumbing.
    // Title rules are checked before class mappings.
    void activateProfileForWindow(const std::string& window_class, const std::string& title = {});

    // The shortcut set that should be active for this window, or empty.
    [[nodiscard]] std::string shortcutForWindow(const std::string& window_class,
                                                const std::string& title) const;
    void activateProfile(const std::string& profile);
    void setDrawList(const std::vector<std::size_t>& list);
    void applyPresetMasks(const std::vector<std::vector<bool>>& masks);
    void applyPresetMask(std::size_t index, const std::vector<bool>& mask);
    void applyPresetParameter(std::size_t index, const std::string& key, const std::string& value);
    void refreshRender();

    void startConfigWatch();
    void stopConfigWatch();

private:
    void renderLoop();
    void renderAndPush(double time_seconds);

    // Nudges the render thread. Takes no lock so it is safe to call from
    // anywhere, including while engine_mutex_ is held.
    void wake();

    bool applyProfileLocked(const std::string& profile);
    std::string describeStatus();
    std::string describePresets();
    std::string describeProfiles();

    std::string cmdProfile(const std::string& arg);
    std::string cmdToggle(const std::string& arg);
    std::string cmdSet(const std::string& args);
    std::string cmdFrame(const std::string& arg);
    std::string cmdWatch(const std::string& arg);
    std::string cmdBrightness(const std::string& arg);
    std::string cmdReload();
    std::string cmdMetric(const std::string& args);
    std::string cmdState(const std::string& args);
    std::string cmdGame(const std::string& args);
    std::string listGames();

    // A running game owns the whole keyboard; these save and restore the
    // composition it interrupted.
    void applyGameOverrideLocked(std::size_t game_index);
    void clearGameOverrideLocked();

    // Reports whether a freshly loaded config still describes the device we
    // already have open.
    [[nodiscard]] static bool deviceSectionMatches(const KeyboardModel& current,
                                                   const KeyboardModel& fresh);

    KeyboardModel model_;
    std::unique_ptr<DeviceTransport> transport_;
    const ConfigLoader& loader_;
    EffectEngine engine_;
    KeyActivityProviderPtr key_activity_;
    SystemStatePtr system_state_;

    std::string config_path_;
    std::vector<ParameterMap> preset_parameters_;
    std::optional<HyprConfig> hypr_;

    mutable std::mutex engine_mutex_;

    // Render loop.
    std::mutex loop_mutex_;
    std::condition_variable loop_cv_;
    std::thread render_thread_;
    std::atomic<bool> stop_{true};
    std::atomic<bool> dirty_{true};
    std::atomic<int> frame_interval_ms_{33};
    // Master brightness, 0-100, applied when the frame is encoded.
    std::atomic<int> brightness_{100};
    std::chrono::steady_clock::time_point start_time_;

    std::atomic<bool> quit_requested_{false};

    std::function<void(const HyprConfig&)> config_observer_;
    std::mutex reload_mutex_;

    // Config watching.
    std::thread config_watch_thread_;
    bool watch_config_on_start_{false};
    std::atomic<bool> config_watch_enabled_{false};
    std::atomic<bool> config_changed_{false};

    // Current composition, so overrides can be undone.
    std::string active_profile_;
    std::vector<std::size_t> current_draw_list_;
    std::vector<std::vector<bool>> current_masks_;

    bool game_override_active_{false};
    std::string active_game_;
    std::vector<std::size_t> saved_draw_list_;
    std::vector<std::vector<bool>> saved_masks_;
    bool saved_state_valid_{false};
};

}  // namespace kb::cfg
