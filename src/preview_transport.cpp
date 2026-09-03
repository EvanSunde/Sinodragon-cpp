#include "keyboard_configurator/preview_transport.hpp"

#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <sstream>

#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kHideCursor = "\x1b[?25l";
constexpr const char* kShowCursor = "\x1b[?25h";

// Perceived brightness, used to pick a legible label colour on each cell.
bool isDark(RgbColor color) {
    const int luma = (299 * color.r + 587 * color.g + 114 * color.b) / 1000;
    return luma < 128;
}

// Two characters is all a cell has room for, so squeeze the label down to
// something still recognisable.
std::string abbreviate(const std::string& label) {
    if (label == "NAN") {
        return "  ";
    }
    if (label.size() <= 2) {
        std::string out = label;
        out.resize(2, ' ');
        return out;
    }

    static const std::pair<const char*, const char*> kShort[] = {
        {"Backtick", "` "}, {"BracketOpen", "[ "}, {"BracketClose", "] "},
        {"Apostrophe", "' "}, {"Semicolon", "; "}, {"Backslash", "\\ "},
        {"Comma", ", "}, {"Period", ". "}, {"Slash", "/ "},
        {"Minus", "- "}, {"Equal", "= "}, {"Space", "__"},
        {"Enter", "En"}, {"Bksp", "Bs"}, {"Caps", "Cp"},
        {"Shift", "Sh"}, {"Ctrl", "Ct"}, {"PrtSc", "Pr"},
        {"Pause", "Pa"}, {"Home", "Ho"}, {"End", "En"},
        {"PgUp", "PU"}, {"PgDn", "PD"}, {"Left", "<-"},
        {"Right", "->"}, {"Up", "^ "}, {"Down", "v "},
        {"Del", "De"}, {"Win", "Wn"}, {"Alt", "Al"},
        {"Tab", "Tb"}, {"Esc", "Es"},
    };
    for (const auto& [full, shortened] : kShort) {
        if (label == full) {
            return shortened;
        }
    }
    return label.substr(0, 2);
}

}  // namespace

PreviewTransport::PreviewTransport() : is_tty_(::isatty(STDOUT_FILENO) == 1) {}

PreviewTransport::~PreviewTransport() {
    if (is_tty_) {
        std::cout << kShowCursor << std::flush;
    }
}

std::string PreviewTransport::id() const {
    return "preview";
}

bool PreviewTransport::connect(const KeyboardModel& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_tty_) {
        return true;
    }
    std::cout << kHideCursor << "Previewing " << model.name() << " (" << model.keyCount()
              << " keys). Ctrl-C to stop.\n"
              << std::flush;
    return true;
}

bool PreviewTransport::shouldTranspose(const KeyboardModel& model) const {
    if (!transpose_auto_) {
        return transpose_;
    }
    // A layout with far more rows than columns is really storing columns --
    // the packet order runs down the board. Flip it so the preview looks like
    // a keyboard.
    const auto& layout = model.layout();
    std::size_t widest = 0;
    for (const auto& row : layout) {
        widest = std::max(widest, row.size());
    }
    return layout.size() > widest;
}

bool PreviewTransport::sendFrame(const KeyboardModel& model,
                                 const std::vector<std::uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_tty_) {
        if (!warned_not_tty_) {
            warned_not_tty_ = true;
            std::cerr << "[Preview] stdout is not a terminal; nothing to draw.\n";
        }
        return true;
    }

    // Decode the wire payload back into colours so the preview reflects
    // exactly what the keyboard would receive.
    const std::size_t header = model.packetHeader().size();
    std::vector<RgbColor> colors;
    colors.reserve(model.keyCount());
    for (std::size_t i = 0; i < model.keyCount(); ++i) {
        const std::size_t offset = header + i * 3;
        if (offset + 2 >= payload.size()) {
            colors.push_back({0, 0, 0});
            continue;
        }
        colors.push_back({payload[offset], payload[offset + 1], payload[offset + 2]});
    }

    draw(model, colors);
    return true;
}

void PreviewTransport::draw(const KeyboardModel& model, const std::vector<RgbColor>& colors) {
    const auto& layout = model.layout();
    if (layout.empty()) {
        return;
    }

    std::size_t widest = 0;
    for (const auto& row : layout) {
        widest = std::max(widest, row.size());
    }

    const bool flip = shouldTranspose(model);
    const std::size_t rows = flip ? widest : layout.size();
    const std::size_t cols = flip ? layout.size() : widest;

    std::ostringstream out;

    // Redraw over the previous grid instead of scrolling it off the screen.
    if (last_drawn_rows_ > 0) {
        out << "\x1b[" << last_drawn_rows_ << "A";
    }

    for (std::size_t r = 0; r < rows; ++r) {
        out << "\r\x1b[2K";
        for (std::size_t c = 0; c < cols; ++c) {
            const std::size_t layout_row = flip ? c : r;
            const std::size_t layout_col = flip ? r : c;

            if (layout_row >= layout.size() || layout_col >= layout[layout_row].size()) {
                out << "   ";
                continue;
            }

            std::size_t index = 0;
            for (std::size_t i = 0; i < layout_row; ++i) {
                index += layout[i].size();
            }
            index += layout_col;

            const std::string& label = layout[layout_row][layout_col];
            if (label == "NAN") {
                out << "   ";
                continue;
            }

            const RgbColor color = index < colors.size() ? colors[index] : RgbColor{};
            out << "\x1b[48;2;" << static_cast<int>(color.r) << ';' << static_cast<int>(color.g)
                << ';' << static_cast<int>(color.b) << 'm'
                << (isDark(color) ? "\x1b[38;2;170;170;170m" : "\x1b[38;2;20;20;20m")
                << abbreviate(label) << kReset << ' ';
        }
        out << '\n';
    }

    last_drawn_rows_ = rows;
    std::cout << out.str() << std::flush;
}

}  // namespace kb::cfg
