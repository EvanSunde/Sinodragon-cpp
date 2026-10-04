#include "keyboard_configurator/chroma_preset.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <unordered_map>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

// Where each key sits in Chroma's keyboard grid. A Chroma RZKEY value is
// (row << 8) | column, so RZKEY_ESC = 0x0001 is row 0, column 1 -- this table
// is the SDK's RZKEY list, transcribed with the matching evdev keycode and the
// layout labels configs commonly use for the same key.
//
// Cells the SDK reserves for macro keys, the logo LED and the ISO-only keys
// with no stable evdev code are left out, and simply map to nothing.
struct Cell {
    int row;
    int col;
    int keycode;
    std::array<const char*, 3> labels;
};

// clang-format off
constexpr Cell kCells[] = {
    // Row 0: Esc, function row, PrtSc/ScrLk/Pause.
    {0,  1, KEY_ESC,        {"Esc", "Escape", nullptr}},
    {0,  3, KEY_F1,         {"F1", nullptr, nullptr}},
    {0,  4, KEY_F2,         {"F2", nullptr, nullptr}},
    {0,  5, KEY_F3,         {"F3", nullptr, nullptr}},
    {0,  6, KEY_F4,         {"F4", nullptr, nullptr}},
    {0,  7, KEY_F5,         {"F5", nullptr, nullptr}},
    {0,  8, KEY_F6,         {"F6", nullptr, nullptr}},
    {0,  9, KEY_F7,         {"F7", nullptr, nullptr}},
    {0, 10, KEY_F8,         {"F8", nullptr, nullptr}},
    {0, 11, KEY_F9,         {"F9", nullptr, nullptr}},
    {0, 12, KEY_F10,        {"F10", nullptr, nullptr}},
    {0, 13, KEY_F11,        {"F11", nullptr, nullptr}},
    {0, 14, KEY_F12,        {"F12", nullptr, nullptr}},
    {0, 15, KEY_SYSRQ,      {"PrtSc", "PrintScreen", "Print"}},
    {0, 16, KEY_SCROLLLOCK, {"ScrLk", "ScrollLock", "Scroll"}},
    {0, 17, KEY_PAUSE,      {"Pause", "Break", nullptr}},

    // Row 1: number row, Ins/Home/PgUp, numpad top.
    {1,  1, KEY_GRAVE,      {"Backtick", "Grave", "Tilde"}},
    {1,  2, KEY_1,          {"1", nullptr, nullptr}},
    {1,  3, KEY_2,          {"2", nullptr, nullptr}},
    {1,  4, KEY_3,          {"3", nullptr, nullptr}},
    {1,  5, KEY_4,          {"4", nullptr, nullptr}},
    {1,  6, KEY_5,          {"5", nullptr, nullptr}},
    {1,  7, KEY_6,          {"6", nullptr, nullptr}},
    {1,  8, KEY_7,          {"7", nullptr, nullptr}},
    {1,  9, KEY_8,          {"8", nullptr, nullptr}},
    {1, 10, KEY_9,          {"9", nullptr, nullptr}},
    {1, 11, KEY_0,          {"0", nullptr, nullptr}},
    {1, 12, KEY_MINUS,      {"Minus", "-", nullptr}},
    {1, 13, KEY_EQUAL,      {"Equal", "=", nullptr}},
    {1, 14, KEY_BACKSPACE,  {"Bksp", "Backspace", nullptr}},
    {1, 15, KEY_INSERT,     {"Ins", "Insert", nullptr}},
    {1, 16, KEY_HOME,       {"Home", nullptr, nullptr}},
    {1, 17, KEY_PAGEUP,     {"PgUp", "PageUp", nullptr}},
    {1, 18, KEY_NUMLOCK,    {"NumLock", "NumLk", nullptr}},
    {1, 19, KEY_KPSLASH,    {"NumSlash", "KpSlash", nullptr}},
    {1, 20, KEY_KPASTERISK, {"NumStar", "KpAsterisk", nullptr}},
    {1, 21, KEY_KPMINUS,    {"NumMinus", "KpMinus", nullptr}},

    // Row 2: QWERTY row, Del/End/PgDn, numpad 7-9 and +.
    {2,  1, KEY_TAB,        {"Tab", nullptr, nullptr}},
    {2,  2, KEY_Q,          {"Q", nullptr, nullptr}},
    {2,  3, KEY_W,          {"W", nullptr, nullptr}},
    {2,  4, KEY_E,          {"E", nullptr, nullptr}},
    {2,  5, KEY_R,          {"R", nullptr, nullptr}},
    {2,  6, KEY_T,          {"T", nullptr, nullptr}},
    {2,  7, KEY_Y,          {"Y", nullptr, nullptr}},
    {2,  8, KEY_U,          {"U", nullptr, nullptr}},
    {2,  9, KEY_I,          {"I", nullptr, nullptr}},
    {2, 10, KEY_O,          {"O", nullptr, nullptr}},
    {2, 11, KEY_P,          {"P", nullptr, nullptr}},
    {2, 12, KEY_LEFTBRACE,  {"BracketOpen", "LeftBrace", "["}},
    {2, 13, KEY_RIGHTBRACE, {"BracketClose", "RightBrace", "]"}},
    {2, 14, KEY_BACKSLASH,  {"Backslash", "\\", nullptr}},
    {2, 15, KEY_DELETE,     {"Del", "Delete", nullptr}},
    {2, 16, KEY_END,        {"End", nullptr, nullptr}},
    {2, 17, KEY_PAGEDOWN,   {"PgDn", "PageDown", nullptr}},
    {2, 18, KEY_KP7,        {"Num7", "Kp7", nullptr}},
    {2, 19, KEY_KP8,        {"Num8", "Kp8", nullptr}},
    {2, 20, KEY_KP9,        {"Num9", "Kp9", nullptr}},
    {2, 21, KEY_KPPLUS,     {"NumPlus", "KpPlus", nullptr}},

    // Row 3: home row, numpad 4-6.
    {3,  1, KEY_CAPSLOCK,   {"Caps", "CapsLock", nullptr}},
    {3,  2, KEY_A,          {"A", nullptr, nullptr}},
    {3,  3, KEY_S,          {"S", nullptr, nullptr}},
    {3,  4, KEY_D,          {"D", nullptr, nullptr}},
    {3,  5, KEY_F,          {"F", nullptr, nullptr}},
    {3,  6, KEY_G,          {"G", nullptr, nullptr}},
    {3,  7, KEY_H,          {"H", nullptr, nullptr}},
    {3,  8, KEY_J,          {"J", nullptr, nullptr}},
    {3,  9, KEY_K,          {"K", nullptr, nullptr}},
    {3, 10, KEY_L,          {"L", nullptr, nullptr}},
    {3, 11, KEY_SEMICOLON,  {"Semicolon", ";", nullptr}},
    {3, 12, KEY_APOSTROPHE, {"Apostrophe", "Quote", "'"}},
    {3, 14, KEY_ENTER,      {"Enter", "Return", nullptr}},
    {3, 18, KEY_KP4,        {"Num4", "Kp4", nullptr}},
    {3, 19, KEY_KP5,        {"Num5", "Kp5", nullptr}},
    {3, 20, KEY_KP6,        {"Num6", "Kp6", nullptr}},

    // Row 4: shift row, Up, numpad 1-3 and Enter. Cell (4,2) is the ISO key
    // between Shift and Z.
    {4,  1, KEY_LEFTSHIFT,  {"Shift", "LShift", "LeftShift"}},
    {4,  2, KEY_102ND,      {"ISO", "102nd", nullptr}},
    {4,  3, KEY_Z,          {"Z", nullptr, nullptr}},
    {4,  4, KEY_X,          {"X", nullptr, nullptr}},
    {4,  5, KEY_C,          {"C", nullptr, nullptr}},
    {4,  6, KEY_V,          {"V", nullptr, nullptr}},
    {4,  7, KEY_B,          {"B", nullptr, nullptr}},
    {4,  8, KEY_N,          {"N", nullptr, nullptr}},
    {4,  9, KEY_M,          {"M", nullptr, nullptr}},
    {4, 10, KEY_COMMA,      {"Comma", ",", nullptr}},
    {4, 11, KEY_DOT,        {"Period", "Dot", "."}},
    {4, 12, KEY_SLASH,      {"Slash", "/", nullptr}},
    {4, 14, KEY_RIGHTSHIFT, {"RShift", "RightShift", nullptr}},
    {4, 16, KEY_UP,         {"Up", nullptr, nullptr}},
    {4, 18, KEY_KP1,        {"Num1", "Kp1", nullptr}},
    {4, 19, KEY_KP2,        {"Num2", "Kp2", nullptr}},
    {4, 20, KEY_KP3,        {"Num3", "Kp3", nullptr}},
    {4, 21, KEY_KPENTER,    {"NumEnter", "KpEnter", nullptr}},

    // Row 5: modifiers, space, arrows, numpad 0 and decimal.
    {5,  1, KEY_LEFTCTRL,   {"Ctrl", "LCtrl", "LeftCtrl"}},
    {5,  2, KEY_LEFTMETA,   {"Win", "Super", "Meta"}},
    {5,  3, KEY_LEFTALT,    {"Alt", "LAlt", "LeftAlt"}},
    {5,  7, KEY_SPACE,      {"Space", nullptr, nullptr}},
    {5, 11, KEY_RIGHTALT,   {"RAlt", "RightAlt", "AltGr"}},
    {5, 12, KEY_FN,         {"Fn", nullptr, nullptr}},
    {5, 13, KEY_COMPOSE,    {"Menu", "Compose", nullptr}},
    {5, 14, KEY_RIGHTCTRL,  {"RCtrl", "RightCtrl", nullptr}},
    {5, 15, KEY_LEFT,       {"Left", nullptr, nullptr}},
    {5, 16, KEY_DOWN,       {"Down", nullptr, nullptr}},
    {5, 17, KEY_RIGHT,      {"Right", nullptr, nullptr}},
    {5, 19, KEY_KP0,        {"Num0", "Kp0", nullptr}},
    {5, 20, KEY_KPDOT,      {"NumDot", "KpDot", nullptr}},
};
// clang-format on

std::string trimmed(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

// "2,15" or an RZKEY value such as "0x020F". -1 when it is neither, or lies
// outside the grid.
int parseCell(const std::string& text) {
    const std::string spec = trimmed(text);
    int row = -1;
    int col = -1;
    try {
        if (spec.size() > 2 && spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X')) {
            const unsigned long value = std::stoul(spec, nullptr, 16);
            row = static_cast<int>((value >> 8) & 0xFF);
            col = static_cast<int>(value & 0xFF);
        } else {
            const auto comma = spec.find(',');
            if (comma == std::string::npos) {
                return -1;
            }
            row = std::stoi(spec.substr(0, comma));
            col = std::stoi(spec.substr(comma + 1));
        }
    } catch (...) {
        return -1;
    }
    if (row < 0 || row >= ChromaState::kRows || col < 0 || col >= ChromaState::kCols) {
        return -1;
    }
    return row * ChromaState::kCols + col;
}

RgbColor toRgb(std::uint32_t colorref) {
    const ChromaRgb c = unpackColorRef(colorref);
    return {c.r, c.g, c.b};
}

}  // namespace

void ChromaPreset::configure(const ParameterMap& params) {
    fill_mode_ = Fill::Dominant;
    fill_color_ = {0, 0, 0};
    if (auto it = params.find("fill"); it != params.end()) {
        const std::string value = trimmed(it->second);
        if (value == "black" || value == "off") {
            fill_mode_ = Fill::Fixed;
        } else if (!value.empty() && value != "dominant") {
            fill_mode_ = Fill::Fixed;
            fill_color_ = color::hex(value, {0, 0, 0});
        }
    }

    // "Label=row,col;Label=0xRRCC" -- built by the config loader from a
    // [chroma.map] table, or written by hand on a layer.
    overrides_.clear();
    if (auto it = params.find("map"); it != params.end()) {
        std::size_t start = 0;
        const std::string& text = it->second;
        while (start <= text.size()) {
            const auto end = std::min(text.find(';', start), text.size());
            const std::string entry = text.substr(start, end - start);
            const auto eq = entry.rfind('=');
            if (eq != std::string::npos) {
                const std::string label = trimmed(entry.substr(0, eq));
                const int cell = parseCell(entry.substr(eq + 1));
                if (!label.empty() && cell >= 0) {
                    overrides_.emplace_back(label, cell);
                } else if (!label.empty()) {
                    std::cerr << "Warning: chroma map entry '" << trimmed(entry)
                              << "' needs a cell like 2,15 or 0x020F; ignoring it.\n";
                }
            }
            start = end + 1;
        }
    }

    resolved_for_ = nullptr;  // Re-resolve with the new overrides.
}

void ChromaPreset::resolve(const KeyboardModel& model) {
    const std::size_t key_count = model.keyCount();
    key_to_cell_.assign(key_count, -1);

    // Keycodes first: they identify a key whatever the layout file calls it.
    if (model.hasKeycodeMap()) {
        for (const Cell& cell : kCells) {
            if (auto index = model.indexForKeycode(cell.keycode)) {
                if (key_to_cell_[*index] < 0) {
                    key_to_cell_[*index] = cell.row * ChromaState::kCols + cell.col;
                }
            }
        }
    }

    // Labels fill whatever is left, which is everything on a board configured
    // without a keycodes CSV.
    for (const Cell& cell : kCells) {
        for (const char* label : cell.labels) {
            if (label == nullptr) {
                break;
            }
            if (auto index = model.indexForKey(label)) {
                if (key_to_cell_[*index] < 0) {
                    key_to_cell_[*index] = cell.row * ChromaState::kCols + cell.col;
                }
                break;
            }
        }
    }

    // Explicit entries win over both.
    for (const auto& [label, cell] : overrides_) {
        if (auto index = model.indexForKey(label)) {
            key_to_cell_[*index] = cell;
        } else {
            std::cerr << "Warning: chroma map names key '" << label
                      << "', which is not in the layout; ignoring it.\n";
        }
    }

    resolved_for_ = &model;
    resolved_key_count_ = key_count;
}

const std::vector<int>& ChromaPreset::keyToCell(const KeyboardModel& model) {
    if (resolved_for_ != &model || resolved_key_count_ != model.keyCount()) {
        resolve(model);
    }
    return key_to_cell_;
}

void ChromaPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    (void)time_seconds;

    ChromaState::Grid grid{};
    if (!state_ || state_->grid(grid) == 0) {
        frame.fill(fill_mode_ == Fill::Fixed ? fill_color_ : RgbColor{0, 0, 0});
        return;
    }

    const std::vector<int>& map = keyToCell(model);

    // Keys with no cell take the frame's most common colour by default, so a
    // game painting a solid background does not leave holes where this board
    // has keys Chroma's grid lacks.
    RgbColor fill = fill_color_;
    if (fill_mode_ == Fill::Dominant) {
        std::unordered_map<std::uint32_t, int> counts;
        counts.reserve(16);
        std::uint32_t best = 0;
        int best_count = 0;
        for (std::uint32_t value : grid) {
            const int count = ++counts[value & 0x00FFFFFFu];
            if (count > best_count) {
                best_count = count;
                best = value & 0x00FFFFFFu;
            }
        }
        fill = toRgb(best);
    }

    const std::size_t limit = std::min(map.size(), frame.size());
    for (std::size_t key = 0; key < limit; ++key) {
        const int cell = map[key];
        frame.setColor(key, cell >= 0 ? toRgb(grid[static_cast<std::size_t>(cell)]) : fill);
    }
}

}  // namespace kb::cfg
