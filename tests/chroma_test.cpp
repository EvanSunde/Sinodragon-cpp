// Checks that a Chroma frame lands on the right keys, and that the REST
// handler turns each kind of request into the frame it describes.
//
//   ctest --test-dir build        (or: build/chroma_test tests)

#include <iostream>
#include <memory>
#include <string>

#include "keyboard_configurator/chroma_preset.hpp"
#include "keyboard_configurator/chroma_server.hpp"
#include "keyboard_configurator/chroma_state.hpp"
#include "keyboard_configurator/config_loader.hpp"
#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/static_color_preset.hpp"

using namespace kb::cfg;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << '\n';
    if (!ok) {
        ++failures;
    }
}

constexpr std::uint32_t kPink = 5898495;  // Dead Cells' background, 0x5A00FF -> #FF005A
constexpr std::uint32_t kGreen = 65280;   // 0x00FF00

const RgbColor kPinkRgb{0xFF, 0x00, 0x5A};
const RgbColor kGreenRgb{0x00, 0xFF, 0x00};

// The frame Dead Cells sent mid-run: pink everywhere, and a green health bar
// across the number row, cells 1 to 13.
ChromaState::Grid deadCellsFrame() {
    ChromaState::Grid grid;
    grid.fill(kPink);
    for (int col = 1; col <= 13; ++col) {
        grid[1 * ChromaState::kCols + col] = kGreen;
    }
    return grid;
}

PresetRegistry registry() {
    PresetRegistry r;
    r.registerPreset("chroma", [] { return std::make_unique<ChromaPreset>(); });
    r.registerPreset("static_color", [] { return std::make_unique<StaticColorPreset>(); });
    return r;
}

struct Board {
    RuntimeConfig config;
    ChromaPreset* preset;
    ChromaStatePtr state;
};

Board load(const ConfigLoader& loader, const std::string& path) {
    Board board{loader.loadFromFile(path), nullptr, std::make_shared<ChromaState>()};
    const int index = board.config.chroma.preset_index;
    if (index >= 0) {
        board.preset = dynamic_cast<ChromaPreset*>(board.config.presets[static_cast<std::size_t>(index)].get());
        board.preset->setChromaState(board.state);
    }
    return board;
}

RgbColor keyColor(Board& board, const KeyColorFrame& frame, const std::string& label) {
    const auto index = board.config.model.indexForKey(label);
    return index ? frame.color(*index) : RgbColor{1, 2, 3};
}

KeyColorFrame render(Board& board) {
    KeyColorFrame frame(board.config.model.keyCount());
    board.preset->render(board.config.model, 0.0, frame);
    return frame;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "tests";
    const auto reg = registry();
    ConfigLoader loader(reg);

    // --- Config -------------------------------------------------------------
    Board keycoded = load(loader, dir + "/chroma_keycodes.toml");
    check(keycoded.config.chroma.port == 54299, "[chroma] port is read");
    check(keycoded.config.chroma.timeout.count() == 3000, "[chroma] timeout is read in seconds");
    check(keycoded.config.chroma.mode == "auto", "mode defaults to auto");
    check(keycoded.preset != nullptr, "a built-in chroma layer is appended");

    // --- Mapping by keycode ---------------------------------------------------
    keycoded.state->setGrid(deadCellsFrame());
    KeyColorFrame frame = render(keycoded);
    check(keyColor(keycoded, frame, "Backtick") == kGreenRgb, "cell 1,1 lands on Backtick");
    check(keyColor(keycoded, frame, "1") == kGreenRgb, "cell 1,2 lands on 1");
    check(keyColor(keycoded, frame, "0") == kGreenRgb, "cell 1,11 lands on 0");
    check(keyColor(keycoded, frame, "Equal") == kGreenRgb, "cell 1,13 lands on Equal");
    check(keyColor(keycoded, frame, "Bksp") == kPinkRgb, "cell 1,14 (Backspace) stays background");
    check(keyColor(keycoded, frame, "Q") == kPinkRgb, "the QWERTY row stays background");
    check(keyColor(keycoded, frame, "Esc") == kPinkRgb, "COLORREF is unpacked as BGR (#FF005A)");

    // An explicit [chroma.map] entry beats the table: Pause is mapped onto
    // Esc's cell, so it follows Esc rather than its own cell.
    ChromaState::Grid marked;
    marked.fill(0);
    marked[0 * ChromaState::kCols + 1] = 0x0000FF;   // Esc's cell: red
    marked[0 * ChromaState::kCols + 17] = 0xFF0000;  // Pause's own cell: blue
    keycoded.state->setGrid(marked);
    frame = render(keycoded);
    check(keyColor(keycoded, frame, "Esc") == (RgbColor{0xFF, 0, 0}), "0x0000FF is red, not blue");
    check(keyColor(keycoded, frame, "Pause") == (RgbColor{0xFF, 0, 0}), "[chroma.map] overrides the table");

    // --- Mapping by label, and fixed fill -------------------------------------
    Board labelled = load(loader, dir + "/chroma_labels.toml");
    check(!labelled.config.model.hasKeycodeMap(), "second board has no keycodes CSV");
    check(labelled.config.chroma.mode == "layer", "mode = layer is read");
    labelled.state->setGrid(deadCellsFrame());
    frame = render(labelled);
    check(keyColor(labelled, frame, "5") == kGreenRgb, "labels alone still place the number row");
    check(keyColor(labelled, frame, "Space") == kPinkRgb, "labels place Space");

    labelled.state->clear();
    frame = render(labelled);
    check(keyColor(labelled, frame, "5") == (RgbColor{0x10, 0x20, 0x30}),
          "no frame -> the configured fill colour");

    labelled.state->setGrid(deadCellsFrame());
    labelled.state->setEnabled(false);
    frame = render(labelled);
    check(keyColor(labelled, frame, "5") == (RgbColor{0x10, 0x20, 0x30}), "chroma off shows nothing");

    // --- REST handler ----------------------------------------------------------
    auto state = std::make_shared<ChromaState>();
    int frames = 0;
    int idles = 0;
    ChromaServer::Callbacks callbacks;
    callbacks.on_frame = [&] { ++frames; };
    callbacks.on_idle = [&] { ++idles; };
    ChromaServer server(state, 54299, std::chrono::milliseconds(3000), callbacks);

    auto reply = server.handleForTest("GET", "/razer/chromasdk", "");
    check(reply.status == 200 && reply.body.find("version") != std::string::npos, "GET returns the SDK version");

    reply = server.handleForTest("POST", "/razer/chromasdk",
                                 R"({"title":"Dead Cells","description":"x","category":"game"})");
    const auto at = reply.body.find("\"sessionid\":");
    check(reply.status == 200 && at != std::string::npos, "registering returns a session id");
    const std::string session = reply.body.substr(at + 12, reply.body.find(',', at) - at - 12);
    const std::string base = "/razer/chromasdk/" + session;

    std::string custom = R"({"effect":"CHROMA_CUSTOM","param":[)";
    for (int r = 0; r < 6; ++r) {
        custom += (r ? ",[" : "[");
        for (int c = 0; c < 22; ++c) {
            custom += (c ? "," : "") + std::to_string((r == 1 && c >= 1 && c <= 13) ? kGreen : kPink);
        }
        custom += "]";
    }
    custom += "]}";
    reply = server.handleForTest("PUT", base + "/keyboard", custom);
    ChromaState::Grid got{};
    check(reply.status == 200 && frames == 1 && state->grid(got) != 0 && got == deadCellsFrame(),
          "PUT CHROMA_CUSTOM stores the grid and fires on_frame");
    check(state->status().app == "Dead Cells", "the frame is credited to the registered title");

    reply = server.handleForTest("PUT", base + "/keyboard", R"({"effect":"CHROMA_STATIC","param":{"color":255}})");
    state->grid(got);
    check(frames == 2 && got[0] == 255 && got[131] == 255, "CHROMA_STATIC fills the grid");

    // CUSTOM_KEY: a key entry with its high byte set wins over the background.
    std::string custom_key = R"({"effect":"CHROMA_CUSTOM_KEY","param":{"color":[[1,1]],"key":[[0,16777471]]}})";
    server.handleForTest("PUT", base + "/keyboard", custom_key);
    state->grid(got);
    check(got[0] == 1 && got[1] == 255, "CHROMA_CUSTOM_KEY applies flagged keys and strips the flag");

    reply = server.handleForTest("PUT", base + "/keyboard", R"({"effect":"CHROMA_WAVE","param":{}})");
    check(reply.status == 200 && frames == 3, "an animated effect is accepted but changes nothing");

    // Two-phase: POST creates, PUT /effect applies.
    reply = server.handleForTest("POST", base + "/keyboard", R"({"effect":"CHROMA_STATIC","param":{"color":65280}})");
    const auto id_at = reply.body.find("\"id\":\"");
    check(id_at != std::string::npos && frames == 3, "POST creates an effect without showing it");
    const std::string id = reply.body.substr(id_at + 6, reply.body.find('"', id_at + 6) - id_at - 6);
    server.handleForTest("PUT", base + "/effect", "{\"id\":\"" + id + "\"}");
    state->grid(got);
    check(frames == 4 && got[0] == 65280, "PUT /effect shows a created effect");

    reply = server.handleForTest("PUT", base + "/mouse", R"({"effect":"CHROMA_STATIC","param":{"color":1}})");
    check(reply.status == 200 && frames == 4, "other devices are accepted and ignored");

    reply = server.handleForTest("PUT", "/razer/chromasdk/424242/keyboard", R"({"effect":"CHROMA_NONE"})");
    check(reply.status == 200 && frames == 5, "an unknown session is adopted, not refused");

    reply = server.handleForTest("PUT", base + "/keyboard", "{not json");
    check(reply.status == 400, "malformed JSON is rejected");
    reply = server.handleForTest("PUT", base + "/keyboard", std::string(200, '[') + std::string(200, ']'));
    check(reply.status == 400, "deeply nested JSON is rejected rather than recursed into");

    server.handleForTest("DELETE", base, "");
    check(idles == 0, "one app leaving while another is live keeps the takeover");
    server.handleForTest("DELETE", "/razer/chromasdk/424242", "");
    check(idles == 1 && state->grid(got) == 0, "the last app leaving fires on_idle and clears the frame");

    std::cout << (failures == 0 ? "\nall passed\n" : "\nFAILURES: " + std::to_string(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
