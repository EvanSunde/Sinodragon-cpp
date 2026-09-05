#include "keyboard_configurator/pomodoro_preset.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

void PomodoroPreset::configure(const ParameterMap& params) {
    auto minutes = [&](const char* key, double& target) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(0.1, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    minutes("work_minutes", work_minutes_);
    minutes("short_break_minutes", short_break_minutes_);
    minutes("long_break_minutes", long_break_minutes_);

    if (auto it = params.find("rounds_before_long_break"); it != params.end()) {
        try {
            rounds_before_long_break_ = std::max(1, std::stoi(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("flash_seconds"); it != params.end()) {
        try {
            flash_seconds_ = std::max(0.0, std::stod(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("bar_keys"); it != params.end()) {
        bar_keys_ = color::splitList(it->second);
        resolved_valid_ = false;
    }

    auto colour = [&](const char* key, RgbColor& target) {
        if (auto it = params.find(key); it != params.end()) {
            target = color::hex(it->second, target);
        }
    };
    colour("color_work", color_work_);
    colour("color_short_break", color_short_break_);
    colour("color_long_break", color_long_break_);
    colour("color_spent", color_spent_);
    colour("color_paused", color_paused_);
}

double PomodoroPreset::phaseLength() const {
    switch (phase_) {
        case Phase::ShortBreak: return short_break_minutes_ * 60.0;
        case Phase::LongBreak:  return long_break_minutes_ * 60.0;
        case Phase::Work:       break;
    }
    return work_minutes_ * 60.0;
}

double PomodoroPreset::remaining() const {
    double spent = elapsed_in_phase_;
    if (running_) {
        spent += std::chrono::duration<double>(Clock::now() - resumed_at_).count();
    }
    return std::max(0.0, phaseLength() - spent);
}

RgbColor PomodoroPreset::phaseColor() const {
    switch (phase_) {
        case Phase::ShortBreak: return color_short_break_;
        case Phase::LongBreak:  return color_long_break_;
        case Phase::Work:       break;
    }
    return color_work_;
}

void PomodoroPreset::start() {
    if (running_) {
        return;
    }
    running_ = true;
    resumed_at_ = Clock::now();
    phase_ended_at_ = -1.0;  // starting acknowledges any finished-phase flash
}

void PomodoroPreset::pause() {
    if (!running_) {
        return;
    }
    // Freeze the elapsed time so resuming picks up exactly where we stopped.
    elapsed_in_phase_ += std::chrono::duration<double>(Clock::now() - resumed_at_).count();
    running_ = false;
}

void PomodoroPreset::reset() {
    running_ = false;
    phase_ = Phase::Work;
    completed_work_rounds_ = 0;
    elapsed_in_phase_ = 0.0;
    phase_ended_at_ = -1.0;
}

void PomodoroPreset::advancePhase() {
    if (phase_ == Phase::Work) {
        ++completed_work_rounds_;
        phase_ = (completed_work_rounds_ % rounds_before_long_break_ == 0) ? Phase::LongBreak
                                                                          : Phase::ShortBreak;
    } else {
        phase_ = Phase::Work;
    }
    elapsed_in_phase_ = 0.0;
    resumed_at_ = Clock::now();
}

void PomodoroPreset::skip() {
    advancePhase();
    phase_ended_at_ = -1.0;
}

std::string PomodoroPreset::statusLine() const {
    const double left = remaining();
    const int minutes = static_cast<int>(left) / 60;
    const int seconds = static_cast<int>(left) % 60;

    std::ostringstream out;
    switch (phase_) {
        case Phase::Work:       out << "work"; break;
        case Phase::ShortBreak: out << "short break"; break;
        case Phase::LongBreak:  out << "long break"; break;
    }
    out << ' ' << minutes << 'm' << (seconds < 10 ? "0" : "") << seconds << 's';
    out << (running_ ? " remaining" : " remaining (paused)");
    out << ", round " << (completed_work_rounds_ % rounds_before_long_break_) + 1 << '/'
        << rounds_before_long_break_;
    return out.str();
}

void PomodoroPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_spent_);

    if (!resolved_valid_) {
        resolved_.clear();
        for (const auto& label : bar_keys_) {
            if (auto index = model.indexForKey(label)) {
                resolved_.push_back(*index);
            }
        }
        if (resolved_.empty()) {
            // No bar_keys given: use the whole board, so the layer's own
            // zones/keys mask decides where the bar shows.
            for (std::size_t i = 0; i < model.keyCount(); ++i) {
                resolved_.push_back(i);
            }
        }
        resolved_valid_ = true;
    }

    const double length = phaseLength();
    const double left = remaining();

    // Phase boundary: stop the clock and flash until the next start.
    if (running_ && left <= 0.0) {
        running_ = false;
        elapsed_in_phase_ = length;
        phase_ended_at_ = time_seconds;
        advancePhase();
        // advancePhase resets elapsed; hold the new phase paused until started.
        running_ = false;
        elapsed_in_phase_ = 0.0;
    }

    if (phase_ended_at_ >= 0.0 && time_seconds - phase_ended_at_ < flash_seconds_) {
        // "Time's up": pulse the whole bar in the colour of the phase you are
        // about to begin.
        const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 8.0);
        for (std::size_t index : resolved_) {
            frame.setColor(index, mixColors(color_spent_, phaseColor(), pulse));
        }
        return;
    }
    if (phase_ended_at_ >= 0.0 && time_seconds - phase_ended_at_ >= flash_seconds_) {
        phase_ended_at_ = -1.0;
    }

    const double fraction = (length > 0.0) ? std::clamp(left / length, 0.0, 1.0) : 0.0;
    const double filled = fraction * static_cast<double>(resolved_.size());
    const RgbColor lit = running_ ? phaseColor() : mixColors(phaseColor(), color_paused_, 0.6);

    for (std::size_t i = 0; i < resolved_.size(); ++i) {
        // The bar drains from the far end; the last cell fades out
        // proportionally so the countdown moves smoothly.
        const double fill = std::clamp(filled - static_cast<double>(i), 0.0, 1.0);
        if (fill <= 0.0) {
            continue;
        }
        frame.setColor(resolved_[i], mixColors(color_spent_, lit, fill));
    }
}

}  // namespace kb::cfg
