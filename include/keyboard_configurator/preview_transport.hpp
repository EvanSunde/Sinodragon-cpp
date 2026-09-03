#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "keyboard_configurator/device_transport.hpp"

namespace kb::cfg {

// Draws each frame as truecolour blocks laid out in the physical key grid, so
// effects can be developed and tuned with no keyboard plugged in. It decodes
// the same payload that would have gone to the device, so what you see includes
// the packet's NAN zeroing and the master brightness.
class PreviewTransport : public DeviceTransport {
public:
    PreviewTransport();
    ~PreviewTransport() override;

    std::string id() const override;
    bool connect(const KeyboardModel& model) override;
    bool sendFrame(const KeyboardModel& model,
                   const std::vector<std::uint8_t>& payload) override;

    // Layout files store the packet order, which for some keyboards runs down
    // the columns rather than along the rows. Transposing puts the grid the
    // right way up on screen. Auto-detected, overridable from the config.
    void setTranspose(bool transpose) { transpose_ = transpose; }
    void setTransposeAuto() { transpose_auto_ = true; }

private:
    void draw(const KeyboardModel& model, const std::vector<RgbColor>& colors);
    [[nodiscard]] bool shouldTranspose(const KeyboardModel& model) const;

    std::mutex mutex_;
    bool is_tty_{false};
    bool warned_not_tty_{false};
    bool transpose_{false};
    bool transpose_auto_{true};
    std::size_t last_drawn_rows_{0};
};

}  // namespace kb::cfg
