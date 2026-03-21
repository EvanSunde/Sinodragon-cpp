#pragma once

#include "keyboard_configurator/preset.hpp"
#include <random>
#include <string>
#include <vector>

namespace kb::cfg {

struct Vector2 {
    double x = 0.0;
    double y = 0.0;
};

struct Node {
    Vector2 pos;
    int parent_idx = -1;
    double thickness = 1.0;
    double dist_from_root = 0.0;
    double birth_time = 0.0;
    double opacity = 1.0;
    double strength = 1.0; // For reinforcement tracking
};

class SpaceColonizationPreset : public LightingPreset {
public:
    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;

    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override
    {
        key_activity_provider_ = provider;
    }

private:
    void reset();
    void cleanupDeadNodes(double now);
    void grow(double now);
    void applyKeyActivityInjection(double now);
    void buildCoords(const KeyboardModel& model);

    // Configuration
    int attractor_count_ = 500;
    double kill_dist_ = 0.02;
    double influence_dist_ = 0.15;
    double segment_len_ = 0.015;

    // Timing & Life
    double growth_interval_ = 0.05;
    double lifespan_ = 2.0;
    double fade_time_ = 1.0;

    // Visuals
    RgbColor color_root_ = { 255, 50, 50 };
    RgbColor color_tip_ = { 255, 200, 200 };
    double thickness_base_ = 0.03;
    double thickness_decay_ = 0.98;

    // Interactive & Safety
    bool reactive_enabled_ = true;
    std::string interaction_mode_ = "root";
    double trigger_proximity_ = 0.01; // Distance to ignore repeat taps
    KeyActivityProviderPtr key_activity_provider_;

    // State Tracking
    std::vector<Vector2> attractors_;
    std::vector<Node> nodes_;
    double last_growth_time_ = 0.0;
    double internal_time_ = 0.0;
    double last_real_time_ = 0.0;

    // Render Cache
    std::vector<double> xs_;
    std::vector<double> ys_;
    bool coords_built_ = false;
};

} // namespace kb::cfg
