#include "keyboard_configurator/game_preset.hpp"

#include <algorithm>
#include <cctype>

#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

void GameBoard::build(const KeyboardModel& model) {
    const auto& layout = model.layout();
    width_ = 0;
    height_ = 0;
    cell_to_key_.clear();
    if (layout.empty()) {
        return;
    }

    std::size_t widest = 0;
    for (const auto& row : layout) {
        widest = std::max(widest, row.size());
    }

    // More layout rows than columns means the file is storing columns, so the
    // physical board is the transpose of it.
    const bool transposed = layout.size() > widest;
    width_ = static_cast<int>(transposed ? layout.size() : widest);
    height_ = static_cast<int>(transposed ? widest : layout.size());

    cell_to_key_.assign(static_cast<std::size_t>(width_) * height_, -1);

    // Running offset, because layout rows are not required to be equal length.
    std::vector<std::size_t> row_offsets(layout.size(), 0);
    std::size_t offset = 0;
    for (std::size_t r = 0; r < layout.size(); ++r) {
        row_offsets[r] = offset;
        offset += layout[r].size();
    }

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const std::size_t row = static_cast<std::size_t>(transposed ? x : y);
            const std::size_t col = static_cast<std::size_t>(transposed ? y : x);
            if (row >= layout.size() || col >= layout[row].size()) {
                continue;
            }
            if (layout[row][col] == "NAN") {
                continue;
            }
            cell_to_key_[static_cast<std::size_t>(y) * width_ + x] =
                static_cast<int>(row_offsets[row] + col);
        }
    }
}

std::optional<std::size_t> GameBoard::index(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return std::nullopt;
    }
    const int key = cell_to_key_[static_cast<std::size_t>(y) * width_ + x];
    if (key < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(key);
}

void GameInput::attach(KeyActivityProviderPtr provider) {
    provider_ = std::move(provider);
    reset();
}

void GameInput::reset() {
    last_poll_ = provider_ ? provider_->nowSeconds() : 0.0;
}

std::vector<std::size_t> GameInput::pollIndices(const KeyboardModel& model) {
    std::vector<std::size_t> out;
    if (!provider_) {
        return out;
    }

    const double now = provider_->nowSeconds();
    // Ask for exactly the window since the last poll so no press is handled
    // twice and none is missed between frames.
    const double window = std::max(0.0, now - last_poll_);
    last_poll_ = now;

    for (const auto& event : provider_->recentEvents(window)) {
        if (event.key_index < model.keyCount()) {
            out.push_back(event.key_index);
        }
    }
    return out;
}

std::vector<std::string> GameInput::poll(const KeyboardModel& model) {
    std::vector<std::string> out;
    for (std::size_t index : pollIndices(model)) {
        out.push_back(normalizeKeyLabel(model.keyLabels()[index]));
    }
    return out;
}

std::string normalizeKeyLabel(const std::string& label) {
    std::string upper;
    upper.reserve(label.size());
    for (char ch : label) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    // Layout files and evdev disagree on spelling; fold the aliases games use.
    if (upper == "KEY_UP" || upper == "ARROWUP") return "UP";
    if (upper == "KEY_DOWN" || upper == "ARROWDOWN") return "DOWN";
    if (upper == "KEY_LEFT" || upper == "ARROWLEFT") return "LEFT";
    if (upper == "KEY_RIGHT" || upper == "ARROWRIGHT") return "RIGHT";
    if (upper == "KEY_SPACE" || upper == "SPACEBAR") return "SPACE";
    if (upper == "KEY_ENTER" || upper == "RETURN") return "ENTER";
    if (upper == "KEY_ESC" || upper == "ESCAPE") return "ESC";
    return upper;
}

}  // namespace kb::cfg
