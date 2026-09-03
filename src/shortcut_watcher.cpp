#include "keyboard_configurator/shortcut_watcher.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cerrno>
#include <algorithm>
#include <vector>

#include <libevdev/libevdev.h>

#include "keyboard_configurator/runtime.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {
static inline bool is_ctrl(int code) { return code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL; }
static inline bool is_shift(int code) { return code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT; }
static inline bool is_alt(int code) { return code == KEY_LEFTALT || code == KEY_RIGHTALT; }
static inline bool is_super(int code) { return code == KEY_LEFTMETA || code == KEY_RIGHTMETA; }
}

ShortcutWatcher::ShortcutWatcher(const KeyboardModel& model,
                                 Runtime& runtime,
                                 const HyprConfig& hypr,
                                 std::size_t key_count)
    : model_(model), runtime_(runtime), hypr_(hypr), key_count_(key_count) {
    compileShortcuts();
}

// Resolving every key label to an index up front keeps the hot path -- a
// modifier press -- a single hash lookup.
void ShortcutWatcher::compileShortcuts() {
    overlay_valid_ = hypr_.shortcuts_overlay_preset_index >= 0;
    overlay_index_ = overlay_valid_ ? static_cast<std::size_t>(hypr_.shortcuts_overlay_preset_index) : 0;

    compiled_.clear();
    for (const auto& [name, profile] : hypr_.shortcuts) {
        CompiledProfile compiled;
        for (const auto& [modmask, labels] : profile.combos) {
            std::vector<std::size_t> indices;
            indices.reserve(labels.size());
            for (const auto& label : labels) {
                if (auto idx = model_.indexForKey(label)) {
                    indices.push_back(*idx);
                }
            }
            compiled.combos.emplace(modmask, std::move(indices));
        }
        compiled_.emplace(name, std::move(compiled));
    }
}

void ShortcutWatcher::reconfigure(const HyprConfig& hypr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    hypr_ = hypr;
    compileShortcuts();
    updateActiveShortcutFromClass();
    // engaged_ refers to preset indices that no longer exist after a reload.
    engaged_ = false;
    applyMaskForMods(mods_.load());
}

ShortcutWatcher::~ShortcutWatcher() { stop(); }

void ShortcutWatcher::start() {
    if (!overlay_valid_) return;
    if (thread_.joinable()) return;
    stop_.store(false);
    openDevices();
    thread_ = std::thread(&ShortcutWatcher::runLoop, this);
}

void ShortcutWatcher::stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    closeDevices();
}

bool ShortcutWatcher::setActiveWindow(const std::string& window_class, const std::string& title) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    active_class_ = window_class;
    active_title_ = title;
    updateActiveShortcutFromClass();
    
    // If we are NOT currently showing shortcuts, we might need to update the background profile immediately
    // But usually HyprlandWatcher handles the normal switching.
    // We only need to care if we ARE showing shortcuts, to ensure the "restore" target is correct.
    
    // Re-apply mods to ensure logic stays consistent
    applyMaskForMods(mods_.load());
    
    return engaged_;
}

void ShortcutWatcher::openDevices() {
    devices_.clear();
    const std::filesystem::path by_path("/dev/input/by-path");
    std::error_code ec;
    if (!std::filesystem::exists(by_path, ec)) return;
    
    for (auto& entry : std::filesystem::directory_iterator(by_path, ec)) {
        if (ec) break;
        if (!entry.is_symlink(ec) && !entry.is_regular_file(ec)) continue;
        const auto name = entry.path().filename().string();
        if (name.find("-kbd") == std::string::npos) continue;
        
        std::filesystem::path real = std::filesystem::read_symlink(entry.path(), ec);
        std::filesystem::path node = real.empty() ? entry.path() : (entry.path().parent_path() / real);
        
        int fd = ::open(node.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        libevdev* dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) != 0) {
            ::close(fd);
            continue;
        }
        devices_.push_back({fd, dev});
    }
}

void ShortcutWatcher::closeDevices() {
    for (auto& d : devices_) {
        if (d.dev) {
            libevdev_free(d.dev);
            d.dev = nullptr;
        }
        if (d.fd >= 0) {
            ::close(d.fd);
            d.fd = -1;
        }
    }
    devices_.clear();
}

void ShortcutWatcher::runLoop() {
    updateActiveShortcutFromClass();
    applyMaskForMods(0);

    while (!stop_.load()) {
        int combined = 0;
        for (auto& d : devices_) {
            if (!d.dev) continue;
            while (true) {
                input_event ev{};
                int rc = libevdev_next_event(d.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                if (rc == 0) {
                    if (ev.type == EV_KEY) {
                        if (is_ctrl(ev.code)) {
                            if (ev.value) d.mask |= 1; else d.mask &= ~1;
                        } else if (is_shift(ev.code)) {
                            if (ev.value) d.mask |= 2; else d.mask &= ~2;
                        } else if (is_alt(ev.code)) {
                            if (ev.value) d.mask |= 4; else d.mask &= ~4;
                        } else if (is_super(ev.code)) {
                            if (ev.value) d.mask |= 8; else d.mask &= ~8;
                        }
                    }
                } else if (rc == -EAGAIN || rc == LIBEVDEV_READ_STATUS_SYNC || rc == -EINTR) {
                    break;
                } else {
                    break;
                }
            }
            combined |= d.mask;
        }
        if (combined != mods_.load()) {
            mods_.store(combined);
            applyMaskForMods(combined);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void ShortcutWatcher::updateActiveShortcutFromClass() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // The runtime owns the class/title rules, so title-based shortcut
    // selection and hot reload both work without duplicating them here.
    std::string name = runtime_.shortcutForWindow(active_class_, active_title_);
    if (name.empty()) name = hypr_.default_shortcut;
    
    active_shortcut_name_ = name;
    
    // If overlay currently active, update its color immediately
    if (overlay_valid_ && engaged_) {
        auto sit = hypr_.shortcuts.find(active_shortcut_name_);
        if (sit != hypr_.shortcuts.end() && !sit->second.color.empty()) {
            runtime_.applyPresetParameter(overlay_index_, "color", sit->second.color);
        }
    }
}

// Recomputes the profile that should be showing for the focused window,
// rather than restoring a snapshot that may since have gone stale.
void ShortcutWatcher::restoreActiveProfile() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    runtime_.activateProfileForWindow(active_class_, active_title_);
}

void ShortcutWatcher::applyMaskForMods(int modmask) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!overlay_valid_) return;

    // Calculate mask based on active shortcut profile + mods
    std::vector<bool> mask(key_count_, false);
    std::string used_profile;
    
    auto build_from = [&](const std::string& pname) -> bool {
        if (pname.empty()) return false;
        auto it = compiled_.find(pname);
        if (it == compiled_.end()) return false;
        auto jt = it->second.combos.find(modmask);
        if (jt == it->second.combos.end()) return false;
        for (auto idx : jt->second) {
            if (idx < mask.size()) mask[idx] = true;
        }
        used_profile = pname;
        return true;
    };
    
    bool found = build_from(active_shortcut_name_);
    if (!found && hypr_.default_shortcut != active_shortcut_name_) {
        found = build_from(hypr_.default_shortcut);
    }

    const bool has_any = std::any_of(mask.begin(), mask.end(), [](bool b){ return b; });

    if (modmask != 0 && has_any) {
        // === ENGAGE SHORTCUTS ===
        if (!engaged_) {
            // Force DrawList to ONLY be the overlay preset
            std::vector<std::size_t> overlay_only = { overlay_index_ };
            runtime_.setDrawList(overlay_only);
            engaged_ = true;
        }

        // Update Color if needed
        if (!used_profile.empty()) {
            auto sit = hypr_.shortcuts.find(used_profile);
            if (sit != hypr_.shortcuts.end() && !sit->second.color.empty()) {
                runtime_.applyPresetParameter(overlay_index_, "color", sit->second.color);
            }
        }
        
        // Update Mask (Show specific keys)
        runtime_.applyPresetMask(overlay_index_, mask);
        runtime_.refreshRender();
        
    } else {
        // === DISENGAGE (RESTORE) ===
        if (engaged_) {
            // Instead of restoring a saved list, we recalculate the correct list
            // for the current active window.
            restoreActiveProfile();
            
            // Clean up overlay state
            runtime_.applyPresetMask(overlay_index_, std::vector<bool>(key_count_, false));
            engaged_ = false;
        }
    }
}

} // namespace kb::cfg