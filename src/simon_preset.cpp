#include "keyboard_configurator/simon_preset.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

}  // namespace

void SimonPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("pads"); it != params.end()) {
        auto keys = color::splitList(it->second);
        if (keys.size() >= 2) {
            pad_keys_.clear();
            for (auto& key : keys) {
                pad_keys_.push_back(normalizeKeyLabel(key));
            }
            pads_resolved_ = false;
        }
    } else {
        for (auto& key : pad_keys_) {
            key = normalizeKeyLabel(key);
        }
    }
    if (auto it = params.find("pad_colors"); it != params.end()) {
        auto parsed = color::palette(it->second);
        if (!parsed.empty()) {
            pad_colors_ = std::move(parsed);
        }
    }
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("step_seconds", step_seconds_, 0.05);
    number("gap_seconds", gap_seconds_, 0.0);
    number("result_seconds", result_seconds_, 0.1);
    if (auto it = params.find("idle_level"); it != params.end()) {
        try {
            idle_level_ = std::clamp(std::stod(it->second), 0.0, 1.0);
        } catch (...) {
        }
    }
    if (auto it = params.find("background"); it != params.end()) {
        color_background_ = color::hex(it->second, color_background_);
    }
}

void SimonPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void SimonPreset::extendSequence() {
    std::uniform_int_distribution<std::size_t> pick(0, pad_keys_.size() - 1);
    sequence_.push_back(static_cast<int>(pick(rng())));
    ++round_;
}

void SimonPreset::restart(double now) {
    sequence_.clear();
    input_position_ = 0;
    round_ = 0;
    extendSequence();
    phase_ = Phase::Playback;
    phase_started_ = now;
}

void SimonPreset::startGame(const KeyboardModel& model) {
    (void)model;
    pads_resolved_ = false;
    if (pad_colors_.empty()) {
        pad_colors_ = {{255, 60, 60}, {70, 220, 255}, {255, 220, 60},
                       {90, 255, 120}, {200, 110, 255}, {255, 150, 50}};
    }
    input_.reset();
    restart(0.0);
    running_ = true;
}

void SimonPreset::stopGame() {
    running_ = false;
}

void SimonPreset::handleInput(const KeyboardModel& model, double now) {
    for (const auto& key : input_.poll(model)) {
        if (phase_ != Phase::Input) {
            continue;
        }
        // Only the pad keys matter; anything else is ignored rather than fatal.
        const auto pad = std::find(pad_keys_.begin(), pad_keys_.end(), key);
        if (pad == pad_keys_.end()) {
            continue;
        }
        const int pressed = static_cast<int>(std::distance(pad_keys_.begin(), pad));

        if (pressed != sequence_[input_position_]) {
            phase_ = Phase::Wrong;
            phase_started_ = now;
            return;
        }
        if (++input_position_ >= sequence_.size()) {
            phase_ = Phase::Correct;
            phase_started_ = now;
            return;
        }
    }
}

void SimonPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_) {
        return;
    }

    if (!pads_resolved_) {
        pad_indices_.clear();
        std::vector<std::string> kept;
        for (const auto& label : pad_keys_) {
            if (auto index = model.indexForKey(label)) {
                pad_indices_.push_back(*index);
                kept.push_back(label);
            }
        }
        // Drop pads whose key is not on this keyboard rather than indexing past
        // the end later.
        pad_keys_ = kept;
        pads_resolved_ = true;
        if (pad_keys_.size() < 2) {
            return;  // nothing playable
        }
        restart(time_seconds);
    }
    if (pad_keys_.size() < 2) {
        return;
    }

    if (phase_started_ == 0.0) {
        phase_started_ = time_seconds;
    }
    const double elapsed = time_seconds - phase_started_;

    const auto padColor = [&](std::size_t pad) {
        return pad_colors_[pad % pad_colors_.size()];
    };

    // Every pad glows faintly so you can see where they are.
    for (std::size_t pad = 0; pad < pad_indices_.size(); ++pad) {
        frame.setColor(pad_indices_[pad],
                       mixColors(color_background_, padColor(pad), idle_level_));
    }

    switch (phase_) {
        case Phase::Playback: {
            const double per_step = step_seconds_ + gap_seconds_;
            const auto step = static_cast<std::size_t>(elapsed / per_step);
            if (step >= sequence_.size()) {
                phase_ = Phase::Input;
                phase_started_ = time_seconds;
                input_position_ = 0;
                break;
            }
            // Light the current pad for step_seconds_, then go dark for the gap.
            if (elapsed - static_cast<double>(step) * per_step < step_seconds_) {
                const std::size_t pad = static_cast<std::size_t>(sequence_[step]);
                frame.setColor(pad_indices_[pad], padColor(pad));
            }
            break;
        }
        case Phase::Input: {
            handleInput(model, time_seconds);
            // Progress shown by lighting the pads already entered correctly.
            for (std::size_t i = 0; i < input_position_ && i < sequence_.size(); ++i) {
                const std::size_t pad = static_cast<std::size_t>(sequence_[i]);
                frame.setColor(pad_indices_[pad],
                               mixColors(color_background_, padColor(pad), 0.45));
            }
            break;
        }
        case Phase::Correct: {
            const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 14.0);
            for (std::size_t pad = 0; pad < pad_indices_.size(); ++pad) {
                frame.setColor(pad_indices_[pad],
                               mixColors(color_background_, padColor(pad), pulse));
            }
            if (elapsed > result_seconds_) {
                extendSequence();
                input_position_ = 0;
                phase_ = Phase::Playback;
                phase_started_ = time_seconds;
            }
            break;
        }
        case Phase::Wrong: {
            const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 16.0);
            for (std::size_t index : pad_indices_) {
                frame.setColor(index, mixColors(color_background_, color_wrong_, pulse));
            }
            if (elapsed > result_seconds_) {
                restart(time_seconds);
            }
            break;
        }
    }
}

}  // namespace kb::cfg
