#include "keyboard_configurator/runtime.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "keyboard_configurator/config_watcher.hpp"
#include "keyboard_configurator/retry_helper.hpp"
#include "keyboard_configurator/snake_preset.hpp"

namespace kb::cfg {

namespace {

std::string trimCopy(const std::string& in) {
    const auto begin = in.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = in.find_last_not_of(" \t\r\n");
    return in.substr(begin, end - begin + 1);
}

// Splits "set 3 speed 0.9" into ("set", "3 speed 0.9") so that a command can
// decide for itself how to treat the rest of the line. The old CLI read every
// argument with >>, which meant a value could never contain a space.
std::pair<std::string, std::string> splitCommand(const std::string& line) {
    const std::string trimmed = trimCopy(line);
    const auto space = trimmed.find_first_of(" \t");
    if (space == std::string::npos) {
        return {trimmed, {}};
    }
    return {trimmed.substr(0, space), trimCopy(trimmed.substr(space + 1))};
}

bool parseIndex(const std::string& text, std::size_t& out) {
    try {
        std::size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);
        if (consumed != text.size() || value < 0) {
            return false;
        }
        out = static_cast<std::size_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

Runtime::Runtime(RuntimeConfig config, std::string config_path)
    : model_(std::move(config.model)),
      transport_(std::move(config.transport)),
      engine_(model_, *transport_),
      key_activity_(std::make_shared<KeyActivityProvider>(model_.keyCount())),
      config_path_(std::move(config_path)),
      preset_parameters_(std::move(config.preset_parameters)),
      hypr_(std::move(config.hypr)) {
    frame_interval_ms_.store(std::max(1, static_cast<int>(config.frame_interval.count())));
    brightness_.store(std::clamp(config.brightness, 0, 100));

    engine_.setKeyActivityProvider(key_activity_);
    engine_.setPresets(std::move(config.presets), std::move(config.preset_masks));
    engine_.setLayerStyles(std::move(config.preset_styles));
    for (std::size_t i = 0; i < config.preset_enabled.size(); ++i) {
        engine_.setPresetEnabled(i, config.preset_enabled[i]);
    }
}

Runtime::~Runtime() {
    stop();
}

bool Runtime::connect() {
    RetryHelper retry;
    return retry.executeWithRetry([this]() { return transport_->connect(model_); },
                                  "Device connection");
}

std::size_t Runtime::presetCount() const {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    return engine_.presetCount();
}

void Runtime::start() {
    if (!stop_.load()) {
        return;
    }

    // Light the keyboard up straight away rather than waiting for the first
    // window-change event, which may never arrive.
    if (hypr_ && !hypr_->default_profile.empty()) {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        applyProfileLocked(hypr_->default_profile);
    }

    stop_.store(false);
    dirty_.store(true);
    start_time_ = std::chrono::steady_clock::now();
    render_thread_ = std::thread(&Runtime::renderLoop, this);
}

void Runtime::stop() {
    stopConfigWatch();

    if (stop_.exchange(true)) {
        return;
    }
    loop_cv_.notify_all();
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
}

void Runtime::wake() {
    dirty_.store(true);
    loop_cv_.notify_all();
}

void Runtime::requestQuit() {
    quit_requested_.store(true);
    loop_cv_.notify_all();
}

void Runtime::renderLoop() {
    std::unique_lock<std::mutex> lock(loop_mutex_);
    while (!stop_.load()) {
        bool animated = false;
        {
            std::lock_guard<std::mutex> guard(engine_mutex_);
            animated = engine_.hasAnimatedEnabled();
        }

        const bool was_dirty = dirty_.exchange(false);

        if (animated || was_dirty) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
            lock.unlock();
            renderAndPush(elapsed);
            lock.lock();
        }

        if (stop_.load()) {
            break;
        }

        // When something is animating we tick at the frame interval. When
        // nothing is, we park on the condition variable and only re-check
        // periodically, so an idle daemon costs nothing. The timeout means a
        // notify that races with the wait can never strand the loop.
        const auto wait_for = animated ? std::chrono::milliseconds(std::max(1, frame_interval_ms_.load()))
                                       : std::chrono::milliseconds(200);
        loop_cv_.wait_for(lock, wait_for, [this]() { return stop_.load() || dirty_.load(); });
    }
}

void Runtime::renderAndPush(double time_seconds) {
    std::vector<std::uint8_t> payload;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        engine_.renderFrame(time_seconds);
        payload = model_.encodeFrame(engine_.frame(), brightness_.load() / 100.0);
    }
    // Device I/O happens outside the engine lock: a stalled USB write must
    // never block a command coming in from the CLI or the control socket.
    transport_->sendFrame(model_, payload);
}

void Runtime::blank() {
    std::vector<std::uint8_t> payload;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        KeyColorFrame dark(model_.keyCount());
        dark.fill({0, 0, 0});
        payload = model_.encodeFrame(dark);
    }
    transport_->sendFrame(model_, payload);
}

// --- Composition -----------------------------------------------------------

bool Runtime::applyProfileLocked(const std::string& profile) {
    if (!hypr_) {
        return false;
    }
    auto order = hypr_->profile_draw_order.find(profile);
    auto masks = hypr_->profile_masks.find(profile);
    if (order == hypr_->profile_draw_order.end() || masks == hypr_->profile_masks.end()) {
        return false;
    }

    std::vector<std::vector<bool>> resized = masks->second;
    resized.resize(engine_.presetCount(), std::vector<bool>(model_.keyCount(), true));

    active_profile_ = profile;

    if (snake_override_active_) {
        // A game owns the display; remember what to go back to instead.
        saved_draw_list_ = order->second;
        saved_masks_ = resized;
        saved_state_valid_ = true;
        return true;
    }

    current_masks_ = resized;
    current_draw_list_ = order->second;
    engine_.setPresetMasks(resized, true);
    engine_.setDrawList(order->second);
    return true;
}

void Runtime::activateProfile(const std::string& profile) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        applyProfileLocked(profile);
    }
    wake();
}

void Runtime::setDrawList(const std::vector<std::size_t>& list) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (snake_override_active_) {
            saved_draw_list_ = list;
            saved_state_valid_ = true;
        } else {
            current_draw_list_ = list;
            engine_.setDrawList(list);
        }
    }
    wake();
}

void Runtime::applyPresetMasks(const std::vector<std::vector<bool>>& masks) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (snake_override_active_) {
            saved_masks_ = masks;
            saved_state_valid_ = true;
        } else {
            current_masks_ = masks;
            engine_.setPresetMasks(masks, true);
        }
    }
    wake();
}

void Runtime::applyPresetMask(std::size_t index, const std::vector<bool>& mask) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (index >= engine_.presetCount()) {
            return;
        }
        if (snake_override_active_) {
            saved_masks_.resize(engine_.presetCount(), std::vector<bool>(model_.keyCount(), true));
            saved_masks_[index] = mask;
            saved_state_valid_ = true;
        } else {
            current_masks_.resize(engine_.presetCount(), std::vector<bool>(model_.keyCount(), true));
            current_masks_[index] = mask;
            engine_.setPresetMask(index, mask);
        }
    }
    wake();
}

void Runtime::applyPresetParameter(std::size_t index, const std::string& key, const std::string& value) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (index >= engine_.presetCount()) {
            return;
        }
        preset_parameters_.resize(engine_.presetCount());
        if (preset_parameters_[index][key] == value) {
            return;
        }
        preset_parameters_[index][key] = value;
        engine_.presetAt(index).configure(preset_parameters_[index]);
    }
    wake();
}

void Runtime::refreshRender() {
    wake();
}

// --- Commands --------------------------------------------------------------

std::string Runtime::execute(const std::string& line) {
    const auto [cmd, args] = splitCommand(line);
    if (cmd.empty()) {
        return {};
    }

    if (cmd == "help") {
        return "Commands:\n"
               "  help                      show this help\n"
               "  status                    daemon and device state\n"
               "  list                      list presets\n"
               "  profiles                  list configured profiles\n"
               "  profile <name>            activate a profile\n"
               "  toggle <index>            toggle a preset on/off\n"
               "  set <index> <key> <value> set a preset parameter\n"
               "  frame <ms>                animation frame interval\n"
               "  brightness [0-100]        get or set master brightness\n"
               "  snake <start|stop>        start or stop the snake game\n"
               "  watch <on|off>            watch the config file for changes\n"
               "  quit                      shut the daemon down";
    }
    if (cmd == "status") {
        return describeStatus();
    }
    if (cmd == "list") {
        return describePresets();
    }
    if (cmd == "profiles") {
        return describeProfiles();
    }
    if (cmd == "profile") {
        return cmdProfile(args);
    }
    if (cmd == "toggle") {
        return cmdToggle(args);
    }
    if (cmd == "set") {
        return cmdSet(args);
    }
    if (cmd == "frame") {
        return cmdFrame(args);
    }
    if (cmd == "brightness") {
        return cmdBrightness(args);
    }
    if (cmd == "snake") {
        return cmdSnake(args);
    }
    if (cmd == "watch") {
        return cmdWatch(args);
    }
    if (cmd == "quit" || cmd == "exit") {
        requestQuit();
        return "Shutting down.";
    }
    return "Unknown command '" + cmd + "'. Try 'help'.";
}

std::string Runtime::describeStatus() {
    std::ostringstream out;
    std::lock_guard<std::mutex> guard(engine_mutex_);
    out << "device:    " << model_.name() << " (" << std::hex << model_.vendorId() << ':'
        << model_.productId() << std::dec << ")\n";
    out << "transport: " << transport_->id()
        << (transport_->isConnected() ? " (connected)" : " (disconnected, retrying)") << '\n';
    out << "keys:      " << model_.keyCount() << '\n';
    out << "presets:   " << engine_.presetCount() << '\n';
    out << "profile:   " << (active_profile_.empty() ? "(none)" : active_profile_) << '\n';
    out << "layers:    " << current_draw_list_.size() << '\n';
    out << "animated:  " << (engine_.hasAnimatedEnabled() ? "yes" : "no") << '\n';
    out << "interval:  " << frame_interval_ms_.load() << " ms\n";
    out << "brightness: " << brightness_.load() << "%\n";
    out << "watching:  " << (config_watch_enabled_.load() ? config_path_ : std::string("off"));
    return out.str();
}

std::string Runtime::describePresets() {
    std::ostringstream out;
    std::lock_guard<std::mutex> guard(engine_mutex_);
    const auto count = engine_.presetCount();
    out << "Presets (" << count << "):";
    for (std::size_t i = 0; i < count; ++i) {
        const auto& preset = engine_.presetAt(i);
        const bool drawn = std::find(current_draw_list_.begin(), current_draw_list_.end(), i) !=
                           current_draw_list_.end();
        out << "\n  [" << i << "] " << preset.id();
        out << (drawn ? " (drawn" : " (idle");
        if (preset.isAnimated()) {
            out << ", animated";
        }
        out << ')';
    }
    return out.str();
}

std::string Runtime::describeProfiles() {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (!hypr_ || hypr_->profile_draw_order.empty()) {
        return "No profiles configured.";
    }
    std::vector<std::pair<std::string, std::size_t>> profiles;
    profiles.reserve(hypr_->profile_draw_order.size());
    for (const auto& [name, order] : hypr_->profile_draw_order) {
        profiles.emplace_back(name, order.size());
    }
    std::sort(profiles.begin(), profiles.end());

    std::ostringstream out;
    out << "Profiles (" << profiles.size() << "):";
    for (const auto& [name, layers] : profiles) {
        out << "\n  " << name << "  (" << layers << " layers)";
        if (name == active_profile_) {
            out << "  <- active";
        }
    }
    return out.str();
}

std::string Runtime::cmdProfile(const std::string& arg) {
    if (arg.empty()) {
        return "Usage: profile <name>";
    }
    bool applied = false;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        applied = applyProfileLocked(arg);
    }
    if (!applied) {
        return "No such profile: " + arg;
    }
    wake();
    return "Activated profile " + arg;
}

std::string Runtime::cmdToggle(const std::string& arg) {
    std::size_t index = 0;
    if (!parseIndex(arg, index)) {
        return "Usage: toggle <index>";
    }
    bool enabled = false;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (index >= engine_.presetCount()) {
            return "Invalid preset index " + arg;
        }
        enabled = !engine_.presetEnabled(index);
        engine_.setPresetEnabled(index, enabled);

        // Toggling by hand means the user is driving the stack manually, so
        // step out of the profile's draw list and honour the enabled flags.
        current_draw_list_.clear();
        engine_.setDrawList({});
        active_profile_.clear();
    }
    wake();
    return "Preset " + arg + (enabled ? " on" : " off");
}

std::string Runtime::cmdSet(const std::string& args) {
    const auto [index_text, rest] = splitCommand(args);
    const auto [key, value] = splitCommand(rest);
    std::size_t index = 0;
    if (!parseIndex(index_text, index) || key.empty() || value.empty()) {
        return "Usage: set <index> <key> <value>";
    }
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (index >= engine_.presetCount()) {
            return "Invalid preset index " + index_text;
        }
        preset_parameters_.resize(engine_.presetCount());
        preset_parameters_[index][key] = value;
        try {
            engine_.presetAt(index).configure(preset_parameters_[index]);
        } catch (const std::exception& ex) {
            return std::string("Rejected: ") + ex.what();
        }
    }
    wake();
    return "Set preset " + index_text + ' ' + key + " = " + value;
}

std::string Runtime::cmdFrame(const std::string& arg) {
    std::size_t interval = 0;
    if (!parseIndex(arg, interval) || interval == 0) {
        return "Usage: frame <milliseconds>";
    }
    frame_interval_ms_.store(static_cast<int>(interval));
    wake();
    return "Frame interval set to " + arg + " ms";
}

std::string Runtime::cmdBrightness(const std::string& arg) {
    if (arg.empty()) {
        return "Brightness is " + std::to_string(brightness_.load()) + "%";
    }
    std::size_t value = 0;
    if (!parseIndex(arg, value) || value > 100) {
        return "Usage: brightness <0-100>";
    }
    brightness_.store(static_cast<int>(value));
    wake();
    return "Brightness set to " + std::to_string(value) + "%";
}

std::string Runtime::cmdSnake(const std::string& arg) {
    if (arg != "start" && arg != "stop") {
        return "Usage: snake <start|stop>";
    }

    std::string reply;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        std::size_t snake_index = 0;
        SnakePreset* snake = nullptr;
        for (std::size_t i = 0; i < engine_.presetCount(); ++i) {
            if (auto* candidate = dynamic_cast<SnakePreset*>(&engine_.presetAt(i))) {
                snake = candidate;
                snake_index = i;
                break;
            }
        }
        if (snake == nullptr) {
            return "No snake preset configured. Add a profile layer with type = \"snake\".";
        }

        if (arg == "start") {
            snake->start(model_);
            engine_.setPresetEnabled(snake_index, true);
            applySnakeOverrideLocked(snake_index);
            reply = "Snake started. Arrow keys steer; Enter restarts after a crash.";
        } else {
            snake->stop();
            engine_.setPresetEnabled(snake_index, false);
            clearSnakeOverrideLocked();
            reply = "Snake stopped.";
        }
    }
    wake();
    return reply;
}

void Runtime::applySnakeOverrideLocked(std::size_t snake_index) {
    if (!snake_override_active_) {
        saved_draw_list_ = current_draw_list_;
        saved_masks_ = current_masks_;
        saved_state_valid_ = true;
        snake_override_active_ = true;
    }

    const std::vector<std::size_t> only = {snake_index};
    engine_.setDrawList(only);
    current_draw_list_ = only;
    engine_.setPresetMask(snake_index, std::vector<bool>(model_.keyCount(), true));
}

void Runtime::clearSnakeOverrideLocked() {
    if (!snake_override_active_) {
        return;
    }
    snake_override_active_ = false;

    if (saved_state_valid_) {
        if (saved_masks_.size() == engine_.presetCount()) {
            engine_.setPresetMasks(saved_masks_, true);
            current_masks_ = saved_masks_;
        }
        engine_.setDrawList(saved_draw_list_);
        current_draw_list_ = saved_draw_list_;
    }

    saved_state_valid_ = false;
    saved_draw_list_.clear();
    saved_masks_.clear();
}

// --- Config watching -------------------------------------------------------

std::string Runtime::cmdWatch(const std::string& arg) {
    if (arg == "on") {
        if (config_watch_enabled_.load()) {
            return "Config watch already on.";
        }
        startConfigWatch();
        return "Watching " + config_path_ + " for changes.";
    }
    if (arg == "off") {
        if (!config_watch_enabled_.load()) {
            return "Config watch already off.";
        }
        stopConfigWatch();
        return "Config watch off.";
    }
    return "Usage: watch <on|off>";
}

void Runtime::startConfigWatch() {
    if (config_watch_enabled_.exchange(true)) {
        return;
    }
    config_changed_.store(false);
    config_watch_thread_ = std::thread([this]() {
        ConfigWatcher watcher(config_path_);
        while (config_watch_enabled_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!config_watch_enabled_.load()) {
                break;
            }
            if (watcher.hasChanged()) {
                config_changed_.store(true);
                loop_cv_.notify_all();
                break;
            }
        }
    });
}

void Runtime::stopConfigWatch() {
    if (!config_watch_enabled_.exchange(false)) {
        return;
    }
    if (config_watch_thread_.joinable()) {
        config_watch_thread_.join();
    }
}

}  // namespace kb::cfg
