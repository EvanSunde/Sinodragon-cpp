#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

// Presets that take over the whole keyboard while they run. The runtime gives
// one of these an exclusive draw list on start and restores the previous
// composition on stop, so a game never has to know what it interrupted.
class GamePreset : public LightingPreset {
public:
    virtual void startGame(const KeyboardModel& model) = 0;
    virtual void stopGame() = 0;
    [[nodiscard]] virtual bool isGameRunning() const = 0;

    // The name used by `game <name> start`.
    [[nodiscard]] virtual std::string gameName() const = 0;
};

// The keyboard as a rectangular playfield.
//
// Layout files list keys in packet order, which on this hardware runs down the
// physical columns. Games want physical space -- "up" should look like up -- so
// the board transposes when it detects a column-major layout, exactly as the
// terminal preview does.
class GameBoard {
public:
    void build(const KeyboardModel& model);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

    // Key index for a cell, or nullopt when the cell is off-board or is a gap
    // in the physical layout (a NAN entry).
    [[nodiscard]] std::optional<std::size_t> index(int x, int y) const;

    [[nodiscard]] bool playable(int x, int y) const { return index(x, y).has_value(); }

private:
    int width_{0};
    int height_{0};
    std::vector<int> cell_to_key_;  // -1 where there is no key
};

// Reads key presses out of the shared activity bus, one batch per frame.
class GameInput {
public:
    void attach(KeyActivityProviderPtr provider);

    // Labels of the keys pressed since the previous call, oldest first.
    [[nodiscard]] std::vector<std::string> poll(const KeyboardModel& model);

    // Key indices for the same presses, for games that work in board space.
    [[nodiscard]] std::vector<std::size_t> pollIndices(const KeyboardModel& model);

    void reset();

private:
    KeyActivityProviderPtr provider_;
    double last_poll_{0.0};
};

// Uppercases and normalises a key label so games can compare against "UP",
// "SPACE" and so on without caring how the layout file spelled it.
[[nodiscard]] std::string normalizeKeyLabel(const std::string& label);

}  // namespace kb::cfg
