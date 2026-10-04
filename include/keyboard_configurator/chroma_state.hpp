#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace kb::cfg {

// The latest keyboard frame a Razer Chroma SDK application sent us, plus what
// `chroma status` reports about the apps behind it.
//
// ChromaServer writes it as requests arrive; ChromaPreset reads it when the
// render thread composites a frame. It holds data only -- deciding whether to
// take the keyboard over is the Runtime's job.
class ChromaState {
public:
    // Chroma's keyboard model: a fixed 6x22 grid, whatever the physical board.
    static constexpr int kRows = 6;
    static constexpr int kCols = 22;
    static constexpr int kCells = kRows * kCols;

    // COLORREF values, packed 0x00BBGGRR -- blue in the high byte, not red.
    using Grid = std::array<std::uint32_t, kCells>;

    // Stores a frame and returns its sequence number, which starts at 1.
    std::uint64_t setGrid(const Grid& grid);

    // Copies the latest frame into `out`. Returns 0, leaving `out` alone, when
    // no frame has arrived yet or showing is switched off.
    std::uint64_t grid(Grid& out) const;

    // `chroma off`: keep accepting frames, so apps carry on, but show none --
    // in chroma layers inside profiles as well as in the takeover.
    void setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const;

    // Forgets the frame, so layers stop showing a game that has gone away
    // rather than freezing on its last picture.
    void clear();

    struct Status {
        bool listening{false};
        int port{0};
        std::string listen_error;
        std::size_t sessions{0};
        std::string app;  // Title of the app that sent the latest frame.
        std::uint64_t frames{0};
        std::chrono::steady_clock::time_point last_frame{};
    };

    void setListening(int port);
    void setListenError(int port, const std::string& error);
    void setSessions(std::size_t count);
    void noteFrameFrom(const std::string& app);

    [[nodiscard]] Status status() const;

private:
    mutable std::mutex mutex_;
    Grid grid_{};
    std::uint64_t sequence_{0};
    bool enabled_{true};
    Status status_;
};

using ChromaStatePtr = std::shared_ptr<ChromaState>;

// Unpacks a COLORREF. The byte order is the opposite of what the name of every
// other colour type suggests, which makes it the easiest thing here to get
// backwards.
struct ChromaRgb {
    std::uint8_t r, g, b;
};
[[nodiscard]] inline ChromaRgb unpackColorRef(std::uint32_t value) {
    return {static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
            static_cast<std::uint8_t>((value >> 16) & 0xFF)};
}

}  // namespace kb::cfg
