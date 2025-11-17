#include "keyboard_configurator/keyboard_model.hpp"

#include <stdexcept>

#include "keyboard_configurator/key_color_frame.hpp"

namespace kb::cfg {

namespace {
Layout flattenLayout(const KeyboardModel::Layout& layout,
                     std::vector<std::string>& key_labels,
                     std::unordered_map<std::string, std::size_t>& key_to_index) {
    Layout flattened;
    std::size_t index = 0;
    for (const auto& row : layout) {
        KeyboardModel::LayoutRow flat_row;
        for (const auto& key : row) {
            flat_row.push_back(key);
            if (key != "NAN") {
                key_to_index.emplace(key, index);
            }
            key_labels.push_back(key);
            ++index;
        }
        flattened.push_back(std::move(flat_row));
    }
    return flattened;
}
}  // namespace

KeyboardModel::KeyboardModel(std::string name,
                             std::uint16_t vendor_id,
                             std::uint16_t product_id,
                             std::vector<std::uint8_t> packet_header,
                             std::size_t packet_length,
                             Layout layout)
    : name_(std::move(name)),
      vendor_id_(vendor_id),
      product_id_(product_id),
      packet_header_(std::move(packet_header)),
      packet_length_(packet_length) {
    layout_ = flattenLayout(layout, key_labels_, key_to_index_);
}

std::optional<std::size_t> KeyboardModel::indexForKey(const std::string& label) const {
    auto it = key_to_index_.find(label);
    if (it == key_to_index_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::uint8_t> KeyboardModel::encodeFrame(const KeyColorFrame& frame) const {
    if (frame.size() != key_labels_.size()) {
        throw std::runtime_error("Frame size does not match keyboard layout");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(packet_header_.size() + key_labels_.size() * 3);
    payload.insert(payload.end(), packet_header_.begin(), packet_header_.end());

    for (std::size_t idx = 0; idx < key_labels_.size(); ++idx) {
        const auto& label = key_labels_[idx];
        auto color = frame.color(idx);
        if (label == "NAN") {
            color = {0, 0, 0};
        }
        payload.push_back(color.r);
        payload.push_back(color.g);
        payload.push_back(color.b);
    }

    if (payload.size() < packet_length_) {
        payload.resize(packet_length_, 0);
    } else if (payload.size() > packet_length_) {
        throw std::runtime_error("Payload exceeds packet length");
    }

    return payload;
}

}  // namespace kb::cfg
