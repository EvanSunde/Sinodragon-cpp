#include "keyboard_configurator/runtime.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <iostream>
#include <sstream>

#include "keyboard_configurator/config_watcher.hpp"
#include "keyboard_configurator/retry_helper.hpp"
#include "keyboard_configurator/game_preset.hpp"

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

// Parses "30s", "5m", "1h" or a bare number of seconds. Returns false when the
// text is not a duration at all.
bool parseDuration(const std::string& text, std::chrono::seconds& out) {
    if (text.empty()) {
        return false;
    }
    double scale = 1.0;
    std::string digits = text;
    const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
    if (suffix == 's' || suffix == 'm' || suffix == 'h') {
        digits = text.substr(0, text.size() - 1);
        scale = (suffix == 'm') ? 60.0 : (suffix == 'h') ? 3600.0 : 1.0;
    }
    try {
        std::size_t consumed = 0;
        const double value = std::stod(digits, &consumed);
        if (consumed != digits.size() || value <= 0.0) {
            return false;
        }
        out = std::chrono::seconds(static_cast<long long>(value * scale));
        return out.count() > 0;
    } catch (...) {
        return false;
    }
}

}  // namespace

Runtime::Runtime(RuntimeConfig config, std::string config_path, const ConfigLoader& loader)
    : model_(std::move(config.model)),
      transport_(std::move(config.transport)),
      loader_(loader),
      engine_(model_, *transport_),
      key_activity_(std::make_shared<KeyActivityProvider>(model_.keyCount())),
      system_state_(std::make_shared<SystemState>()),
      config_path_(std::move(config_path)),
      preset_parameters_(std::move(config.preset_parameters)),
      hypr_(std::move(config.hypr)) {
    frame_interval_ms_.store(std::max(1, static_cast<int>(config.frame_interval.count())));
    brightness_.store(std::clamp(config.brightness, 0, 100));
    watch_config_on_start_ = config.config_watch_mode;

    engine_.setKeyActivityProvider(key_activity_);
    engine_.setSystemState(system_state_);
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

std::optional<HyprConfig> Runtime::hyprConfig() const {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    return hypr_;
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

    if (watch_config_on_start_) {
        startConfigWatch();
    }
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
        expireTemporaryProfileIfDue();

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

    if (baseOverriddenLocked()) {
        // A game or the shortcut overlay owns the display; remember what to go
        // back to instead of showing it now.
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

namespace {

// Case-insensitive substring test, so a rule for "youtube" matches a title
// reading "YouTube".
bool containsFold(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return false;
    }
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](unsigned char a, unsigned char b) {
                                    return std::tolower(a) == std::tolower(b);
                                });
    return it != haystack.end();
}

}  // namespace

void Runtime::activateProfileForWindow(const std::string& window_class, const std::string& title) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (!hypr_) {
            return;
        }

        std::string profile;

        // Title rules are more specific than class mappings, so they win.
        for (const auto& rule : hypr_->title_rules) {
            if (rule.profile.empty()) {
                continue;
            }
            if (!rule.window_class.empty() && rule.window_class != window_class) {
                continue;
            }
            if (containsFold(title, rule.contains)) {
                profile = rule.profile;
                break;
            }
        }

        if (profile.empty()) {
            auto it = hypr_->class_to_profile.find(window_class);
            profile = (it != hypr_->class_to_profile.end()) ? it->second : hypr_->default_profile;
        }
        if (profile.empty()) {
            return;
        }
        if (temporary_profile_active_) {
            // A hold is in force: remember where the window wants us to go, but
            // do not go there until the hold expires.
            revert_profile_ = profile;
            return;
        }
        applyProfileLocked(profile);
    }
    wake();
}

std::string Runtime::shortcutForWindow(const std::string& window_class,
                                       const std::string& title) const {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (!hypr_) {
        return {};
    }
    for (const auto& rule : hypr_->title_rules) {
        if (rule.shortcut.empty()) {
            continue;
        }
        if (!rule.window_class.empty() && rule.window_class != window_class) {
            continue;
        }
        if (containsFold(title, rule.contains)) {
            return rule.shortcut;
        }
    }
    auto it = hypr_->class_to_shortcut.find(window_class);
    return (it != hypr_->class_to_shortcut.end()) ? it->second : std::string{};
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
        if (baseOverriddenLocked()) {
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
        if (baseOverriddenLocked()) {
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
        if (baseOverriddenLocked()) {
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

bool Runtime::deviceSectionMatches(const KeyboardModel& current, const KeyboardModel& fresh) {
    return current.vendorId() == fresh.vendorId() && current.productId() == fresh.productId() &&
           current.keyCount() == fresh.keyCount() && current.packetLength() == fresh.packetLength();
}

void Runtime::setConfigObserver(std::function<void(const HyprConfig&)> observer) {
    std::lock_guard<std::mutex> guard(reload_mutex_);
    config_observer_ = std::move(observer);
}

std::string Runtime::reload() {
    // Serialised so the file watcher and a `reload` command cannot overlap.
    std::lock_guard<std::mutex> reload_guard(reload_mutex_);

    // RuntimeConfig has no default state (KeyboardModel needs a layout), so it
    // is built inside the optional.
    std::optional<RuntimeConfig> loaded;
    try {
        loaded.emplace(loader_.loadFromFile(config_path_));
    } catch (const std::exception& ex) {
        // A syntax error in a half-saved file must not take the lighting down.
        return std::string("Reload failed, keeping the running config: ") + ex.what();
    }
    RuntimeConfig& fresh = *loaded;

    if (!deviceSectionMatches(model_, fresh.model)) {
        // A different keyboard, layout or packet size needs a new handle, which
        // is more than an in-place swap can do. Ask main for a full restart.
        config_changed_.store(true);
        loop_cv_.notify_all();
        return "The [device] section changed; restarting to reopen the device.";
    }

    std::string profile_to_apply;
    std::size_t layer_count = 0;
    std::optional<HyprConfig> observed;

    {
        std::lock_guard<std::mutex> guard(engine_mutex_);

        engine_.setPresets(std::move(fresh.presets), std::move(fresh.preset_masks));
        engine_.setLayerStyles(std::move(fresh.preset_styles));
        // setPresets rebuilds the preset list, so the shared providers have to
        // be handed to the new instances.
        engine_.setKeyActivityProvider(key_activity_);
        engine_.setSystemState(system_state_);
        for (std::size_t i = 0; i < fresh.preset_enabled.size(); ++i) {
            engine_.setPresetEnabled(i, fresh.preset_enabled[i]);
        }

        preset_parameters_ = std::move(fresh.preset_parameters);
        hypr_ = std::move(fresh.hypr);
        brightness_.store(std::clamp(fresh.brightness, 0, 100));
        frame_interval_ms_.store(std::max(1, static_cast<int>(fresh.frame_interval.count())));

        // Preset indices are rebuilt from scratch, so any override or cached
        // composition pointing at the old ones has to go.
        game_override_active_ = false;
        overlay_active_ = false;
        temporary_profile_active_ = false;
        revert_profile_.clear();
        active_game_.clear();
        saved_state_valid_ = false;
        saved_draw_list_.clear();
        saved_masks_.clear();
        current_draw_list_.clear();
        current_masks_.clear();

        // Stay on the same profile across a reload when it still exists.
        profile_to_apply = active_profile_;
        active_profile_.clear();
        if (hypr_) {
            if (profile_to_apply.empty() ||
                hypr_->profile_draw_order.find(profile_to_apply) == hypr_->profile_draw_order.end()) {
                profile_to_apply = hypr_->default_profile;
            }
            observed = hypr_;
        }
        if (!profile_to_apply.empty()) {
            applyProfileLocked(profile_to_apply);
        }
        layer_count = current_draw_list_.size();
    }

    if (observed && config_observer_) {
        config_observer_(*observed);
    }

    wake();

    std::ostringstream out;
    out << "Reloaded " << config_path_;
    if (!profile_to_apply.empty()) {
        out << "; profile " << profile_to_apply << " (" << layer_count << " layers)";
    }
    return out.str();
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
               "  profile <name> [for <t>]  activate a profile, optionally for 30s/5m/1h\n"
               "  toggle <index>            toggle a preset on/off\n"
               "  set <index> <key> <value> set a preset parameter\n"
               "  frame <ms>                animation frame interval\n"
               "  brightness [0-100]        get or set master brightness\n"
               "  game list                 list configured games\n"
               "  game <name> <start|stop>  run a game (snake, tetris, pong, life)\n"
               "  reload                    re-read the config file in place\n"
               "  metric <name> <0..1>      feed a value to a system_meter layer\n"
               "  state <name> <value>      set a status_light state (ok/warn/fail/busy/off)\n"
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
    if (cmd == "reload") {
        return cmdReload();
    }
    if (cmd == "metric") {
        return cmdMetric(args);
    }
    if (cmd == "state") {
        return cmdState(args);
    }
    if (cmd == "game") {
        return cmdGame(args);
    }
    if (cmd == "snake") {
        // Kept as an alias; `game snake start` is the general form.
        return cmdGame("snake " + (args.empty() ? std::string("start") : args));
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
    if (temporary_profile_active_) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(
                              temporary_profile_expiry_ - std::chrono::steady_clock::now())
                              .count();
        out << "hold:      " << std::max<long long>(0, left) << "s, then "
            << (revert_profile_.empty() ? "default" : revert_profile_) << '\n';
    }
    if (!active_game_.empty()) {
        out << "game:      " << active_game_ << '\n';
    }
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

std::string Runtime::cmdProfile(const std::string& args) {
    if (args.empty()) {
        return "Usage: profile <name> [for <duration>]";
    }

    // profile <name> [for|--for|-f] <30s|5m|1h|seconds>
    const auto [name, rest] = splitCommand(args);
    std::chrono::seconds hold{0};
    if (!rest.empty()) {
        const auto [keyword, duration_text] = splitCommand(rest);
        if (keyword != "for" && keyword != "--for" && keyword != "-f") {
            return "Usage: profile <name> [for <duration>]";
        }
        if (!parseDuration(duration_text, hold)) {
            return "Invalid duration '" + duration_text + "'; try 30s, 5m or 1h";
        }
    }

    bool applied = false;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        // Remember where to go back to before the hold overwrites it.
        const std::string previous = temporary_profile_active_ ? revert_profile_ : active_profile_;
        applied = applyProfileLocked(name);
        if (applied) {
            if (hold.count() > 0) {
                temporary_profile_active_ = true;
                revert_profile_ = previous;
                temporary_profile_expiry_ = std::chrono::steady_clock::now() + hold;
            } else {
                temporary_profile_active_ = false;
                revert_profile_.clear();
            }
        }
    }
    if (!applied) {
        return "No such profile: " + name;
    }
    wake();
    if (hold.count() > 0) {
        return "Activated profile " + name + " for " + std::to_string(hold.count()) + "s";
    }
    return "Activated profile " + name;
}

void Runtime::expireTemporaryProfileIfDue() {
    std::string revert_to;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (!temporary_profile_active_ ||
            std::chrono::steady_clock::now() < temporary_profile_expiry_) {
            return;
        }
        temporary_profile_active_ = false;
        revert_to = revert_profile_;
        revert_profile_.clear();

        if (!revert_to.empty()) {
            applyProfileLocked(revert_to);
        } else if (hypr_ && !hypr_->default_profile.empty()) {
            applyProfileLocked(hypr_->default_profile);
        }
    }
    wake();
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

std::string Runtime::cmdMetric(const std::string& args) {
    const auto [name, value_text] = splitCommand(args);
    if (name.empty() || value_text.empty()) {
        return "Usage: metric <name> <0..1>";
    }
    double value = 0.0;
    try {
        std::size_t consumed = 0;
        value = std::stod(value_text, &consumed);
        if (consumed != value_text.size()) {
            return "Invalid value '" + value_text + "'; expected a number from 0 to 1";
        }
    } catch (...) {
        return "Invalid value '" + value_text + "'; expected a number from 0 to 1";
    }
    system_state_->setMetric(name, value);
    wake();
    return "Metric " + name + " = " + value_text;
}

std::string Runtime::cmdState(const std::string& args) {
    const auto [name, value] = splitCommand(args);
    if (name.empty() || value.empty()) {
        return "Usage: state <name> <ok|warn|fail|busy|off>";
    }
    system_state_->setState(name, value);
    wake();
    return "State " + name + " = " + value;
}

std::string Runtime::cmdReload() {
    return reload();
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

std::string Runtime::listGames() {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    std::vector<std::string> names;
    for (std::size_t i = 0; i < engine_.presetCount(); ++i) {
        if (auto* game = dynamic_cast<GamePreset*>(&engine_.presetAt(i))) {
            names.push_back(game->gameName() + (game->isGameRunning() ? "  <- running" : ""));
        }
    }
    if (names.empty()) {
        return "No games configured. Add a profile layer with type = \"snake\", \"tetris\", "
               "\"pong\" or \"life\".";
    }
    std::sort(names.begin(), names.end());

    std::ostringstream out;
    out << "Games:";
    for (const auto& name : names) {
        out << "\n  " << name;
    }
    return out.str();
}

std::string Runtime::cmdGame(const std::string& args) {
    const auto [first, second] = splitCommand(args);

    if (first.empty() || first == "list") {
        return listGames();
    }

    // `game stop` with no name stops whatever is running.
    const bool stop_anything = (first == "stop" && second.empty());
    const std::string name = stop_anything ? std::string{} : first;
    const std::string action = stop_anything ? std::string("stop")
                                             : (second.empty() ? std::string("start") : second);

    if (action != "start" && action != "stop") {
        return "Usage: game <name> <start|stop>, game stop, or game list";
    }

    std::string reply;
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);

        GamePreset* target = nullptr;
        std::size_t target_index = 0;
        for (std::size_t i = 0; i < engine_.presetCount(); ++i) {
            auto* game = dynamic_cast<GamePreset*>(&engine_.presetAt(i));
            if (game == nullptr) {
                continue;
            }
            if (stop_anything ? game->isGameRunning() : game->gameName() == name) {
                target = game;
                target_index = i;
                break;
            }
        }

        if (target == nullptr) {
            if (stop_anything) {
                return "No game is running.";
            }
            return "No '" + name + "' preset configured. Add a profile layer with type = \"" + name +
                   "\".";
        }

        if (action == "start") {
            // Only one game at a time: the display is exclusive.
            for (std::size_t i = 0; i < engine_.presetCount(); ++i) {
                auto* other = dynamic_cast<GamePreset*>(&engine_.presetAt(i));
                if (other != nullptr && other != target && other->isGameRunning()) {
                    other->stopGame();
                    engine_.setPresetEnabled(i, false);
                }
            }
            // If a modifier is being held, reveal the base first so the game
            // saves the real profile, not the overlay, as what to restore.
            overlayDisengageLocked();
            target->startGame(model_);
            engine_.setPresetEnabled(target_index, true);
            applyGameOverrideLocked(target_index);
            active_game_ = target->gameName();
            reply = "Started " + target->gameName() + ".";
        } else {
            target->stopGame();
            engine_.setPresetEnabled(target_index, false);
            clearGameOverrideLocked();
            reply = "Stopped " + target->gameName() + ".";
            active_game_.clear();
        }
    }
    wake();
    return reply;
}

void Runtime::applyGameOverrideLocked(std::size_t game_index) {
    if (!game_override_active_) {
        saved_draw_list_ = current_draw_list_;
        saved_masks_ = current_masks_;
        saved_state_valid_ = true;
        game_override_active_ = true;
    }

    const std::vector<std::size_t> only = {game_index};
    engine_.setDrawList(only);
    current_draw_list_ = only;
    engine_.setPresetMask(game_index, std::vector<bool>(model_.keyCount(), true));
}

void Runtime::clearGameOverrideLocked() {
    if (!game_override_active_) {
        return;
    }
    game_override_active_ = false;

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

// --- Shortcut overlay -------------------------------------------------------

bool Runtime::overlayEngage(std::size_t preset_index) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (preset_index >= engine_.presetCount() || game_override_active_) {
            return false;  // no overlay over a running game
        }
        if (!overlay_active_) {
            // Save the profile currently showing; it is what disengage reveals,
            // kept current by the window watchers writing to the saved slot.
            saved_draw_list_ = current_draw_list_;
            saved_masks_ = current_masks_;
            saved_state_valid_ = true;
            overlay_active_ = true;
        }
        const std::vector<std::size_t> only = {preset_index};
        engine_.setDrawList(only);
        current_draw_list_ = only;
    }
    wake();
    return true;
}

void Runtime::overlayUpdateMask(std::size_t preset_index, const std::vector<bool>& mask) {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        if (!overlay_active_ || preset_index >= engine_.presetCount()) {
            return;
        }
        // The overlay's own mask is shown live; only base-profile changes are
        // routed to the saved slot.
        engine_.setPresetMask(preset_index, mask);
    }
    wake();
}

void Runtime::overlayDisengageLocked() {
    if (!overlay_active_) {
        return;
    }
    overlay_active_ = false;

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

void Runtime::overlayDisengage() {
    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        overlayDisengageLocked();
    }
    wake();
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
                // Reload in place and keep watching, rather than tearing the
                // whole runtime down and reopening the device.
                std::cout << '\n' << reload() << '\n' << std::flush;
                if (config_changed_.load()) {
                    break;  // device section changed; main will restart us
                }
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
