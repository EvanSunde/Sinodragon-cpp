#pragma once

#include "keyboard_configurator/preset.hpp"
#include <random>
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
    double birth_time = 0.0; // <--- NEW: For fading logic
    double opacity = 1.0; // <--- NEW: Dynamic opacity
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

    // Configuration
    int attractor_count_ = 500;
    double kill_dist_ = 0.02;
    double influence_dist_ = 0.15;
    double segment_len_ = 0.015;

    // Timing & Life
    double growth_interval_ = 0.05; // <--- NEW: Seconds between growth steps (higher = slower)
    double lifespan_ = 2.0; // <--- NEW: How long a vein exists before fading
    double fade_time_ = 1.0; // <--- NEW: How long the fade out takes

    // Visuals
    double zoom_ = 1.0;
    RgbColor color_root_ = { 255, 50, 50 };
    RgbColor color_tip_ = { 255, 200, 200 };
    double thickness_base_ = 0.03;
    double thickness_decay_ = 0.98;

    // Interactive
    bool reactive_enabled_ = true;
    std::string interaction_mode_ = "root"; // "root" or "food"
    double injection_history_ = 0.1;
    KeyActivityProviderPtr key_activity_provider_;

    // State
    std::vector<Vector2> attractors_;
    std::vector<Node> nodes_;
    double last_growth_time_ = 0.0; // <--- NEW: Accumulator for speed control

    // Render Cache
    std::vector<double> xs_;
    std::vector<double> ys_;
    bool coords_built_ = false;

    void buildCoords(const KeyboardModel& model);
};

} // namespace kb::cfg
