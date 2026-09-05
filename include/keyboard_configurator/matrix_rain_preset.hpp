#pragma once

#include <vector>

#include "keyboard_configurator/game_preset.hpp"  // GameBoard: physical grid
#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Falling columns of light with a bright head and a fading tail -- the classic.
// star_matrix twinkles keys independently; this actually falls, which needs the
// physical grid rather than packet order, so it borrows GameBoard.
//
// Drops run down the short axis (six rows) by default, which is what "down"
// means on a keyboard. `direction = "right"` runs them along the long axis
// instead, which reads more like a ticker.
class MatrixRainPreset : public LightingPreset {
public:
    std::string id() const override { return "matrix_rain"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }

private:
    struct Drop {
        double position{0.0};  // head, in cells along the travel axis
        double speed{1.0};
        double length{3.0};
        bool active{false};
        double respawn_at{0.0};
    };

    void ensureLanes(const KeyboardModel& model);

    GameBoard board_;
    std::vector<Drop> lanes_;
    bool lanes_built_{false};

    double last_time_{-1.0};
    double speed_{6.0};        // cells per second
    double speed_variance_{0.5};
    double density_{0.6};      // fraction of lanes active at once
    double tail_{3.0};         // trail length in cells
    bool horizontal_{false};

    RgbColor color_head_{200, 255, 200};
    RgbColor color_tail_{0, 255, 70};
    RgbColor color_background_{0, 8, 0};
};

}  // namespace kb::cfg
