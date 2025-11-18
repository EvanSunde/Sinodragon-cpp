#include "keyboard_configurator/shortcut_watcher.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <cerrno>
#include <algorithm>

#include <libevdev/libevdev.h>

#include "keyboard_configurator/configurator_cli.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {
static inline bool is_ctrl(int code) {
    return code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL;
}
static inline bool is_shift(int code) {
    return code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT;
}
static inline bool is_alt(int code) {
    return code == KEY_LEFTALT || code == KEY_RIGHTALT;
}
static inline bool is_super(int code) {
    return code == KEY_LEFTMETA || code == KEY_RIGHTMETA;
}
}

ShortcutWatcher::ShortcutWatcher(const KeyboardModel& model,
                                 ConfiguratorCLI& cli,
                                 const HyprConfig& hypr,
                                 std::size_t key_count)
    : model_(model), cli_(cli), hypr_(hypr), key_count_(key_count) {
    overlay_index_ = hypr_.shortcuts_overlay_preset_index;
    // Compile shortcut profiles to key indices
    for (const auto& kv : hypr_.shortcuts) {
        CompiledProfile cp;
        // For each combo, translate labels to indices
        for (const auto& ck : kv.second.combos) {
            int modmask = ck.first;
            std::vector<std::size_t> indices;
            indices.reserve(ck.second.size());
            for (const auto& label : ck.second) {
                if (auto idx = model_.indexForKey(label)) {
                    indices.push_back(*idx);
                }
            }
            cp.combos.emplace(modmask, std::move(indices));
        }
        compiled_.emplace(kv.first, std::move(cp));
    }
}

ShortcutWatcher::~ShortcutWatcher() { stop(); }

void ShortcutWatcher::start() {
    if (overlay_index_ < 0) return; // disabled
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

void ShortcutWatcher::setActiveClass(const std::string& klass) {
    active_class_ = klass;
    updateActiveShortcutFromClass();
    // Re-apply current mods to update mask immediately
    applyMaskForMods(mods_.load());
}

void ShortcutWatcher::openDevices() {
    devices_.clear();
    const std::filesystem::path by_path("/dev/input/by-path");
    std::error_code ec;
    if (!std::filesystem::exists(by_path, ec)) {
        return;
    }
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
    // initial state
    updateActiveShortcutFromClass();
    applyMaskForMods(0);

    // Non-blocking poll of devices for events
    while (!stop_.load()) {
        int combined = 0;
        for (auto& d : devices_) {
            if (!d.dev) continue;
            // Drain all pending events
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
                } else if (rc == -EAGAIN) {
                    break;
                } else if (rc == LIBEVDEV_READ_STATUS_SYNC || rc == -EINTR) {
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
    // Determine active shortcut profile name from class
    std::string name;
    auto it = hypr_.class_to_shortcut.find(active_class_);
    if (it != hypr_.class_to_shortcut.end()) name = it->second;
    if (name.empty()) name = hypr_.default_shortcut;
    active_shortcut_name_ = name;
    // Apply color if configured
    if (overlay_index_ >= 0) {
        auto sit = hypr_.shortcuts.find(active_shortcut_name_);
        if (sit != hypr_.shortcuts.end() && !sit->second.color.empty()) {
            cli_.applyPresetParameter(static_cast<std::size_t>(overlay_index_), "color", sit->second.color);
        }
    }
}

void ShortcutWatcher::applyMaskForMods(int modmask) {
    if (overlay_index_ < 0) return;
    // Build mask for current mods
    std::vector<bool> mask(key_count_, false);
    if (!active_shortcut_name_.empty()) {
        auto it = compiled_.find(active_shortcut_name_);
        if (it != compiled_.end()) {
            auto jt = it->second.combos.find(modmask);
            if (jt != it->second.combos.end()) {
                for (auto idx : jt->second) {
                    if (idx < mask.size()) mask[idx] = true;
                }
            }
        }
    }

    const bool has_any = std::any_of(mask.begin(), mask.end(), [](bool b){ return b; });

    if (modmask != 0 && has_any) {
        // Engage exclusive overlay mode: enable only overlay preset
        if (!engaged_) {
            saved_enabled_ = cli_.getPresetEnabledSet();
            std::vector<bool> only_overlay = saved_enabled_;
            if (only_overlay.empty()) {
                // Query again in case
                only_overlay = cli_.getPresetEnabledSet();
            }
        
            only_overlay.assign(only_overlay.size(), false);
            if (overlay_index_ >= 0 && static_cast<std::size_t>(overlay_index_) < only_overlay.size()) {
                only_overlay[static_cast<std::size_t>(overlay_index_)] = true;
            }
            cli_.applyPresetEnableSet(only_overlay);
            engaged_ = true;
        }
        cli_.applyPresetMask(static_cast<std::size_t>(overlay_index_), mask);
        cli_.refreshRender();
    } else {
        // Disengage: clear mask and restore enabled set
        cli_.applyPresetMask(static_cast<std::size_t>(overlay_index_), std::vector<bool>(key_count_, false));
        if (engaged_) {
            if (!saved_enabled_.empty()) {
                cli_.applyPresetEnableSet(saved_enabled_);
            }
            engaged_ = false;
            saved_enabled_.clear();
        }
        cli_.refreshRender();
    }
}

} // namespace kb::cfg
