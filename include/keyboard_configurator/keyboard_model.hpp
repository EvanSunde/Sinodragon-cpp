#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class KeyColorFrame;

class KeyboardModel {
public:
    using LayoutRow = std::vector<std::string>;
    using Layout = std::vector<LayoutRow>;

    KeyboardModel(std::string name,
                  std::uint16_t vendor_id,
                  std::uint16_t product_id,
                  std::vector<std::uint8_t> packet_header,
                  std::size_t packet_length,
                  Layout layout,
                  std::optional<std::uint16_t> interface_usage_page = std::nullopt,
                  std::optional<std::uint16_t> interface_usage = std::nullopt);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::uint16_t vendorId() const noexcept { return vendor_id_; }
    [[nodiscard]] std::uint16_t productId() const noexcept { return product_id_; }
    [[nodiscard]] const std::vector<std::uint8_t>& packetHeader() const noexcept { return packet_header_; }
    [[nodiscard]] std::size_t packetLength() const noexcept { return packet_length_; }
    [[nodiscard]] const Layout& layout() const noexcept { return layout_; }
    [[nodiscard]] const std::vector<std::string>& keyLabels() const noexcept { return key_labels_; }
    [[nodiscard]] std::optional<std::uint16_t> interfaceUsagePage() const noexcept { return interface_usage_page_; }
    [[nodiscard]] std::optional<std::uint16_t> interfaceUsage() const noexcept { return interface_usage_; }

    [[nodiscard]] std::size_t keyCount() const noexcept { return key_labels_.size(); }
    [[nodiscard]] std::optional<std::size_t> indexForKey(const std::string& label) const;
    [[nodiscard]] std::optional<std::size_t> indexForKeycode(int keycode) const;
    [[nodiscard]] bool hasKeycodeMap() const noexcept { return !keycode_to_index_.empty(); }

    void setKeycodeMap(const std::vector<int>& keycodes);

    [[nodiscard]] std::vector<std::uint8_t> encodeFrame(const KeyColorFrame& frame) const;

private:
    std::string name_;
    std::uint16_t vendor_id_;
    std::uint16_t product_id_;
    std::vector<std::uint8_t> packet_header_;
    std::size_t packet_length_;
    Layout layout_;
    std::vector<std::string> key_labels_;
    std::unordered_map<std::string, std::size_t> key_to_index_;
    std::vector<std::size_t> keycode_to_index_;
    std::optional<std::uint16_t> interface_usage_page_;
    std::optional<std::uint16_t> interface_usage_;
};

}  // namespace kb::cfg
