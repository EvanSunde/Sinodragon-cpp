#pragma once

#include <string>
#include <utility>
#include <vector>

#include "keyboard_configurator/chroma_state.hpp"
#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Shows the frames a Razer Chroma SDK application sends -- a game running
// under Wine through the chroma-shim DLL, or anything speaking the Chroma REST
// API to the daemon directly.
//
// Chroma addresses a fixed 6x22 grid. Each cell is resolved to a key on this
// board by evdev keycode when the config has a keycodes CSV, then by layout
// label, then by any explicit `map` entries, so the same frame lands on the
// right keys whatever the physical layout. Keys with no cell get `fill`.
//
// Not animated: it draws only when a new frame arrives and the runtime wakes
// the render loop, so a game sending nothing costs nothing.
class ChromaPreset : public LightingPreset {
public:
    std::string id() const override { return "chroma"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    void setChromaState(ChromaStatePtr state) override { state_ = std::move(state); }

    // Grid cell -> key index for this model, -1 where a key has no cell.
    // Exposed for tests and for `chroma status`.
    [[nodiscard]] const std::vector<int>& keyToCell(const KeyboardModel& model);

private:
    enum class Fill { Dominant, Fixed };

    void resolve(const KeyboardModel& model);

    ChromaStatePtr state_;

    Fill fill_mode_{Fill::Dominant};
    RgbColor fill_color_{0, 0, 0};

    // Explicit "label -> cell" entries from `map`, applied last.
    std::vector<std::pair<std::string, int>> overrides_;

    std::vector<int> key_to_cell_;
    const KeyboardModel* resolved_for_{nullptr};
    std::size_t resolved_key_count_{0};
};

}  // namespace kb::cfg
