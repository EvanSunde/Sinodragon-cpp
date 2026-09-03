#pragma once

#include <memory>
#include <vector>
#include <string>

#include "keyboard_configurator/device_transport.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/key_activity.hpp"
#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class EffectEngine {
public:
    EffectEngine(const KeyboardModel& model, DeviceTransport& transport);

    void setPresets(std::vector<std::unique_ptr<LightingPreset>> presets);
    void setPresets(std::vector<std::unique_ptr<LightingPreset>> presets,
                    std::vector<std::vector<bool>> masks);

    // --- NEW: Painter's Algorithm Support ---
    void setDrawList(std::vector<std::size_t> draw_list);

    // --- MISSING METHOD FIXED HERE ---
    [[nodiscard]] std::size_t presetCount() const { return presets_.size(); }

    void setKeyActivityProvider(KeyActivityProviderPtr provider);

    // Legacy methods
    void setPresetEnabled(std::size_t index, bool enabled);
    bool presetEnabled(std::size_t index) const;
    void setPresetMask(std::size_t index, const std::vector<bool>& mask);
    void setPresetMasks(const std::vector<std::vector<bool>>& masks, bool overlay_replace = false);

    // Per-layer opacity and blend mode. A layer with the default style
    // overwrites the keys its mask covers, which is the historical behaviour.
    void setLayerStyles(std::vector<LayerStyle> styles);
    void setLayerStyle(std::size_t index, const LayerStyle& style);
    [[nodiscard]] LayerStyle layerStyle(std::size_t index) const;

    LightingPreset& presetAt(std::size_t index);
    const LightingPreset& presetAt(std::size_t index) const;

    bool hasAnimatedEnabled() const;
    void renderFrame(double time_seconds);
    bool pushFrame();

    // Exposed so a caller can encode the frame under its own lock and then do
    // the device write without holding it.
    [[nodiscard]] const KeyColorFrame& frame() const noexcept { return frame_; }

private:
    void applyKeyActivityProvider();

    const KeyboardModel& model_;
    DeviceTransport& transport_;
    KeyColorFrame frame_;

    std::vector<std::unique_ptr<LightingPreset>> presets_;
    
    // Caches for performance
    std::vector<std::string> preset_ids_;
    std::vector<bool> preset_animated_;

    // State
    std::vector<bool> preset_enabled_;
    std::vector<std::size_t> active_draw_list_; // New List
    
    std::vector<std::vector<bool>> preset_masks_;
    std::vector<LayerStyle> preset_styles_;
    KeyActivityProviderPtr key_activity_provider_;

    // Scratch buffer for the layer currently being composited. Kept as a
    // member so a frame costs no allocations.
    KeyColorFrame layer_frame_;
};

}  // namespace kb::cfg