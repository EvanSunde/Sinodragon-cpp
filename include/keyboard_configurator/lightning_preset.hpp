#pragma once

#include <vector>

#include "keyboard_configurator/game_preset.hpp"  // GameBoard
#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// A bolt cracks out from the key you press: a jagged main channel that forks,
// flashes white-hot, then fades. Unlike space_colonization (which grows roots
// slowly towards attractors) a bolt is struck all at once and then decays,
// which is what makes it read as lightning rather than as growth.
class LightningPreset : public LightingPreset {
public:
    std::string id() const override { return "lightning"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

private:
    struct Segment {
        int x{0};
        int y{0};
        double intensity{1.0};  // brighter along the main channel than the forks
    };
    struct Bolt {
        std::vector<Segment> segments;
        double born{0.0};
        double life{0.6};
    };

    void strike(const KeyboardModel& model, std::size_t key_index, double now);

    GameBoard board_;
    GameInput input_;
    bool board_built_{false};

    std::vector<Bolt> bolts_;

    double decay_{0.55};        // seconds a bolt lasts
    double branch_chance_{0.35};
    int max_length_{10};
    int max_bolts_{6};
    double flicker_{0.35};
    // Strike on their own now and then, so the board is not dead between keys.
    double ambient_interval_{0.0};
    double next_ambient_{0.0};

    RgbColor color_core_{255, 255, 255};
    RgbColor color_glow_{120, 160, 255};
    RgbColor color_background_{0, 0, 6};
};

}  // namespace kb::cfg
