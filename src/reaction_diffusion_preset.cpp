#include "keyboard_configurator/reaction_diffusion_preset.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace kb::cfg {

namespace {
RgbColor parseHexColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        return {255, 255, 255};
    }
    auto hexToComponent = [](char hi, char lo) -> std::uint8_t {
        auto hexValue = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (upper >= 'A' && upper <= 'F') return 10 + (upper - 'A');
            return 0;
        };
        return static_cast<std::uint8_t>((hexValue(hi) << 4) | hexValue(lo));
    };
    return {
        hexToComponent(value[1], value[2]),
        hexToComponent(value[3], value[4]),
        hexToComponent(value[5], value[6])
    };
}

inline std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
}

std::string ReactionDiffusionPreset::id() const { return "reaction_diffusion"; }

void ReactionDiffusionPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("width"); it != params.end()) width_ = std::max(8, std::stoi(it->second));
    if (auto it = params.find("height"); it != params.end()) height_ = std::max(8, std::stoi(it->second));
    if (auto it = params.find("du"); it != params.end()) du_ = std::stod(it->second);
    if (auto it = params.find("dv"); it != params.end()) dv_ = std::stod(it->second);
    if (auto it = params.find("feed"); it != params.end()) feed_ = std::stod(it->second);
    if (auto it = params.find("kill"); it != params.end()) kill_ = std::stod(it->second);
    if (auto it = params.find("steps"); it != params.end()) steps_per_frame_ = std::max(1, std::stoi(it->second));
    if (auto it = params.find("zoom"); it != params.end()) zoom_ = std::max(0.25, std::stod(it->second));
    if (auto it = params.find("speed"); it != params.end()) speed_ = std::stod(it->second);
    if (auto it = params.find("color_a"); it != params.end()) color_a_ = parseHexColor(it->second);
    if (auto it = params.find("color_b"); it != params.end()) color_b_ = parseHexColor(it->second);

    auto parseBool = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value == "1" || value == "true" || value == "yes" || value == "on";
    };
    if (auto it = params.find("reactive"); it != params.end()) {
        reactive_enabled_ = parseBool(it->second);
    }
    auto parseClamp = [&](const char* key, double& target, double min_v) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                double v = std::stod(it->second);
                target = std::max(min_v, v);
            } catch (...) {
            }
        }
    };
    parseClamp("injection_amount", injection_amount_, 0.0);
    parseClamp("injection_radius", injection_radius_, 0.001);
    parseClamp("injection_decay", injection_decay_, 0.01);
    parseClamp("injection_history", injection_history_, 0.05);
}

void ReactionDiffusionPreset::initGrid() {
    u_.assign(width_ * height_, 1.0);
    v_.assign(width_ * height_, 0.0);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            std::uint32_t h = hash32(static_cast<std::uint32_t>(x + 73856093 * (y + 19349663)));
            double r = static_cast<double>(h % 10000) / 10000.0;
            if (r > 0.98) {
                int idx = y * width_ + x;
                v_[idx] = 1.0;
            }
        }
    }
    inited_ = true;
}

void ReactionDiffusionPreset::step(double dt) {
    std::vector<double> u2(u_), v2(v_);
    auto at = [&](int x, int y) -> int {
        x = (x + width_) % width_;
        y = (y + height_) % height_;
        return y * width_ + x;
    };
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int i = y * width_ + x;
            double u = u_[i];
            double v = v_[i];
            double lap_u = u_[at(x-1,y)] + u_[at(x+1,y)] + u_[at(x,y-1)] + u_[at(x,y+1)] - 4.0 * u;
            double lap_v = v_[at(x-1,y)] + v_[at(x+1,y)] + v_[at(x,y-1)] + v_[at(x,y+1)] - 4.0 * v;
            double uvv = u * v * v;
            u2[i] = u + (du_ * lap_u - uvv + feed_ * (1.0 - u)) * dt;
            v2[i] = v + (dv_ * lap_v + uvv - (kill_ + feed_) * v) * dt;
            u2[i] = std::clamp(u2[i], 0.0, 1.0);
            v2[i] = std::clamp(v2[i], 0.0, 1.0);
        }
    }
    u_.swap(u2);
    v_.swap(v2);
}

void ReactionDiffusionPreset::buildCoords(const KeyboardModel& model) {
    const auto& layout = model.layout();
    std::size_t total = model.keyCount();
    xs_.assign(total, 0.0);
    ys_.assign(total, 0.0);
    std::size_t idx = 0;
    double rows = static_cast<double>(layout.size());
    double max_cols = 1.0;
    for (const auto& row : layout) max_cols = std::max<double>(max_cols, row.size());
    for (std::size_t r = 0; r < layout.size(); ++r) {
        const auto& row = layout[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
            xs_[idx] = max_cols > 1.0 ? static_cast<double>(c) / (max_cols - 1.0) : 0.0;
            ys_[idx] = rows > 1.0 ? static_cast<double>(r) / (rows - 1.0) : 0.0;
            ++idx;
        }
    }
    coords_built_ = true;
}

void ReactionDiffusionPreset::render(const KeyboardModel& model,
                                     double time_seconds,
                                     KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) frame.resize(total);
    if (!inited_) initGrid();
    if (!coords_built_) buildCoords(model);

    applyKeyActivityInjection();

    double dt = 0.5 * speed_;
    for (int s = 0; s < steps_per_frame_; ++s) step(dt);

    for (std::size_t i = 0; i < total; ++i) {
        double x = xs_[i];
        double y = ys_[i];
        double gx = (x * zoom_) * (width_ - 1);
        double gy = (y * zoom_) * (height_ - 1);
        int x0 = static_cast<int>(std::floor(gx));
        int y0 = static_cast<int>(std::floor(gy));
        double tx = gx - x0;
        double ty = gy - y0;
        auto at = [&](int xi, int yi){ xi = (xi + width_) % width_; yi = (yi + height_) % height_; return yi * width_ + xi; };
        double v00 = v_[at(x0, y0)];
        double v10 = v_[at(x0+1, y0)];
        double v01 = v_[at(x0, y0+1)];
        double v11 = v_[at(x0+1, y0+1)];
        double vx0 = v00 * (1.0 - tx) + v10 * tx;
        double vx1 = v01 * (1.0 - tx) + v11 * tx;
        double vxy = vx0 * (1.0 - ty) + vx1 * ty;
        double t = std::clamp(vxy, 0.0, 1.0);
        RgbColor c{};
        c.r = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(color_a_.r * (1.0 - t) + color_b_.r * t)), 0, 255));
        c.g = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(color_a_.g * (1.0 - t) + color_b_.g * t)), 0, 255));
        c.b = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(color_a_.b * (1.0 - t) + color_b_.b * t)), 0, 255));
        frame.setColor(i, c);
    }
}

void ReactionDiffusionPreset::applyKeyActivityInjection() {
    if (!reactive_enabled_ || !key_activity_provider_ || !coords_built_) {
        return;
    }
    const auto total_keys = xs_.size();
    if (total_keys == 0 || width_ <= 0 || height_ <= 0) {
        return;
    }
    const auto events = key_activity_provider_->recentEvents(injection_history_);
    if (events.empty()) {
        return;
    }

    const double now = key_activity_provider_->nowSeconds();
    const double decay = std::max(0.01, injection_decay_);
    const double radius_cells = std::max(1.0, injection_radius_ * std::min(width_, height_));
    const double radius2 = radius_cells * radius_cells;
    const int radius_i = static_cast<int>(std::ceil(radius_cells));

    auto indexAt = [&](int x, int y) {
        x = (x % width_ + width_) % width_;
        y = (y % height_ + height_) % height_;
        return y * width_ + x;
    };

    for (const auto& ev : events) {
        if (ev.key_index >= total_keys) {
            continue;
        }
        double age = std::max(0.0, now - ev.time_seconds);
        double temporal = std::exp(-age / decay);
        double weight = injection_amount_ * ev.intensity * temporal;
        if (weight <= 0.0) {
            continue;
        }
        double gx = xs_[ev.key_index] * (width_ - 1);
        double gy = ys_[ev.key_index] * (height_ - 1);
        int cx = static_cast<int>(std::round(gx));
        int cy = static_cast<int>(std::round(gy));

        for (int dy = -radius_i; dy <= radius_i; ++dy) {
            for (int dx = -radius_i; dx <= radius_i; ++dx) {
                double dist2 = static_cast<double>(dx * dx + dy * dy);
                if (dist2 > radius2) {
                    continue;
                }
                double spatial = std::exp(-dist2 / (radius2 * 0.5 + 1e-6));
                double delta = weight * spatial;
                if (delta <= 0.0) {
                    continue;
                }
                int idx = indexAt(cx + dx, cy + dy);
                u_[idx] = std::clamp(u_[idx] - delta, 0.0, 1.0);
                v_[idx] = std::clamp(v_[idx] + delta, 0.0, 1.0);
            }
        }
    }
}

}  // namespace kb::cfg
