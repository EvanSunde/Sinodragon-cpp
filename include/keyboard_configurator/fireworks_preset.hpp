#pragma once

#include <vector>

#include "keyboard_configurator/game_preset.hpp"  // GameBoard
#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Press a key, a shell launches from the bottom row, arcs up to the key you
// pressed and bursts into drifting sparks that fall and fade.
//
// Particles carry real velocity and gravity rather than being a radial fade,
// which is what makes the burst read as fireworks; sparks are drawn with
// additive blending so overlapping ones brighten.
class FireworksPreset : public LightingPreset {
public:
    std::string id() const override { return "fireworks"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

private:
    struct Particle {
        double x{0.0};
        double y{0.0};
        double vx{0.0};
        double vy{0.0};
        double life{1.0};
        double age{0.0};
        RgbColor color{255, 255, 255};
        bool is_shell{false};  // a rising shell, not yet a spark
        double burst_y{0.0};   // where the shell turns into sparks
    };

    void launch(const KeyboardModel& model, std::size_t key_index);

    GameBoard board_;
    GameInput input_;
    bool board_built_{false};

    std::vector<Particle> particles_;

    double last_time_{-1.0};
    double gravity_{9.0};        // cells per second squared
    double spark_life_{1.1};
    int sparks_per_burst_{14};
    double burst_speed_{5.0};
    double launch_speed_{9.0};
    std::size_t max_particles_{400};
    bool launch_from_bottom_{true};

    std::vector<RgbColor> palette_;
    RgbColor color_background_{0, 0, 0};
};

}  // namespace kb::cfg
