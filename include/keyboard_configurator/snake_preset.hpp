#pragma once

#include "keyboard_configurator/preset.hpp"
#include <deque>
#include <optional>
#include <random>
#include <string>

namespace kb::cfg {

class SnakePreset : public LightingPreset {
public:
    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;

    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void start(const KeyboardModel& model);
    void stop();
    bool isRunning() const { return is_running_; }

private:
    struct Position {
        int r = 0;
        int c = 0;
        bool operator==(const Position& other) const { return r == other.r && c == other.c; }
    };

    enum class Direction { UP, DOWN, LEFT, RIGHT };

    void reset(const KeyboardModel& model);
    void spawnFood(const KeyboardModel& model);
    void updateGame(const KeyboardModel& model);
    void processInput(const KeyboardModel& model);
    bool isValidPosition(const KeyboardModel& model, const Position& pos) const;
    std::optional<Position> advanceWrapped(const KeyboardModel& model,
                                           const Position& from,
                                           Direction dir) const;
    void randomizeColors();

    bool is_running_ = false;
    double step_interval_ = 0.2;
    double last_step_time_ = 0.0;
    double internal_time_ = 0.0;
    double last_real_time_ = 0.0;

    Direction current_dir_ = Direction::RIGHT;
    std::deque<Position> snake_;
    Position food_;
    bool game_over_ = false;
    std::deque<Direction> input_queue_;

    KeyActivityProviderPtr key_activity_provider_;
    double last_input_time_ = 0.0;

    RgbColor color_head_ = { 0, 255, 0 };
    RgbColor color_body_ = { 0, 128, 0 };
    RgbColor color_food_ = { 255, 0, 0 };
    RgbColor color_dead_ = { 255, 0, 0 };
};

} // namespace kb::cfg
