#include "keyboard_configurator/smoke_preset.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <vector>

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

int fastfloor(double x) { return static_cast<int>(x >= 0 ? x : x - 1); }

double lerp(double a, double b, double t) { return a + (b - a) * t; }

double fade(double t) { return t*t*t*(t*(t*6-15)+10); }

int p[512];
bool p_init = false;
int perm_table[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190, 6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168, 68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186, 3,64,
    52,217,226,250,124,123, 5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28, 42,223,183,170,213,
    119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,
    9,129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,
    104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241, 81, 51,145,235,249,14,239,107, 49,192,214, 31,181,199,
    106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,138,
    236,205, 93,222,114, 67,29, 24, 72,243,141,128,195,78,66
};

void ensure_perm() {
    if (p_init) return;
    for (int i = 0; i < 256; ++i) p[i] = perm_table[i];
    for (int i = 0; i < 256; ++i) p[256 + i] = p[i];
    p_init = true;
}

inline double grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

double perlin(double x, double y, double z) {
    ensure_perm();
    int X = fastfloor(x) & 255;
    int Y = fastfloor(y) & 255;
    int Z = fastfloor(z) & 255;
    x -= fastfloor(x);
    y -= fastfloor(y);
    z -= fastfloor(z);
    double u = fade(x);
    double v = fade(y);
    double w = fade(z);
    int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
    int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
    double res = lerp(
        lerp(
            lerp(grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z), u),
            lerp(grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z), u), v),
        lerp(
            lerp(grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1), u),
            lerp(grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1), u), v), w);
    return (res + 1.0) * 0.5;
}
}

std::string SmokePreset::id() const { return "smoke"; }

void SmokePreset::configure(const ParameterMap& params) {
    if (auto it = params.find("speed"); it != params.end()) speed_ = std::stod(it->second);
    if (auto it = params.find("scale"); it != params.end()) scale_ = std::stod(it->second);
    if (auto it = params.find("octaves"); it != params.end()) octaves_ = std::max(1, std::stoi(it->second));
    if (auto it = params.find("persistence"); it != params.end()) persistence_ = std::stod(it->second);
    if (auto it = params.find("lacunarity"); it != params.end()) lacunarity_ = std::stod(it->second);
    if (auto it = params.find("drift_x"); it != params.end()) drift_x_ = std::stod(it->second);
    if (auto it = params.find("drift_y"); it != params.end()) drift_y_ = std::stod(it->second);
    if (auto it = params.find("contrast"); it != params.end()) contrast_ = std::max(0.0, std::stod(it->second));
    if (auto it = params.find("color_low"); it != params.end()) color_low_ = parseHexColor(it->second);
    if (auto it = params.find("color_high"); it != params.end()) color_high_ = parseHexColor(it->second);

    auto parseBool = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return v == "1" || v == "true" || v == "yes" || v == "on";
    };
    if (auto it = params.find("reactive"); it != params.end()) {
        reactive_enabled_ = parseBool(it->second);
    }
    auto parseClamp = [&](const char* key, double& target, double min_v) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                double val = std::stod(it->second);
                target = std::max(min_v, val);
            } catch (...) {
            }
        }
    };
    parseClamp("reactive_history", reactive_history_, 0.05);
    parseClamp("reactive_decay", reactive_decay_, 0.01);
    parseClamp("reactive_spread", reactive_spread_, 0.005);
    parseClamp("reactive_intensity", reactive_intensity_, 0.0);
    parseClamp("reactive_displacement", reactive_displacement_, 0.0);
    parseClamp("reactive_push_duration", reactive_push_duration_, 0.0);
    if (auto it = params.find("reactive_push"); it != params.end()) {
        reactive_push_ = parseBool(it->second);
    }
}

void SmokePreset::buildCoords(const KeyboardModel& model) {
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

void SmokePreset::render(const KeyboardModel& model,
                         double time_seconds,
                         KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) frame.resize(total);
    if (!coords_built_) buildCoords(model);

    std::vector<double> disp_x;
    std::vector<double> disp_y;
    computeReactiveDisplacement(disp_x, disp_y);

    double t_anim = time_seconds * speed_;
    double offset_x = time_seconds * drift_x_;
    double offset_y = time_seconds * drift_y_;
    for (std::size_t i = 0; i < total; ++i) {
        double base_x = xs_[i];
        double base_y = ys_[i];
        if (i < disp_x.size()) {
            base_x = std::clamp(base_x + disp_x[i], 0.0, 1.0);
        }
        if (i < disp_y.size()) {
            base_y = std::clamp(base_y + disp_y[i], 0.0, 1.0);
        }
        double x = base_x * scale_ + offset_x;
        double y = base_y * scale_ + offset_y;
        double amp = 1.0;
        double freq = 1.0;
        double sum = 0.0;
        double norm = 0.0;
        for (int o = 0; o < octaves_; ++o) {
            sum += amp * perlin(x * freq, y * freq, t_anim * freq);
            norm += amp;
            amp *= persistence_;
            freq *= lacunarity_;
        }
        double v = norm > 0.0 ? sum / norm : 0.0;
        // Apply contrast around 0.5 center
        v = 0.5 + (v - 0.5) * contrast_;
        v = std::clamp(v, 0.0, 1.0);
        RgbColor c{};
        c.r = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(color_low_.r * (1.0 - v) + color_high_.r * v)), 0, 255));
        c.g = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(color_low_.g * (1.0 - v) + color_high_.g * v)), 0, 255));
        c.b = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(color_low_.b * (1.0 - v) + color_high_.b * v)), 0, 255));
        frame.setColor(i, c);
    }
}

void SmokePreset::computeReactiveDisplacement(std::vector<double>& dx, std::vector<double>& dy) {
    const std::size_t total = xs_.size();
    dx.assign(total, 0.0);
    dy.assign(total, 0.0);
    if (!reactive_enabled_ || !provider_ || !coords_built_ || total == 0) {
        return;
    }
    const auto events = provider_->recentEvents(reactive_history_);
    if (events.empty()) {
        return;
    }

    const double spread = std::max(0.01, reactive_spread_);
    const double sigma2 = 2.0 * spread * spread;
    const double decay = std::max(0.01, reactive_decay_);
    const double now = provider_->nowSeconds();
    const double base_disp = std::max(0.0, reactive_displacement_);
    const double direction_sign = reactive_push_ ? 1.0 : -1.0;
    const double push_window = std::max(0.0, reactive_push_duration_);

    for (const auto& ev : events) {
        if (ev.key_index >= total) {
            continue;
        }
        const double ex = xs_[ev.key_index];
        const double ey = ys_[ev.key_index];
        const double age = std::max(0.0, now - ev.time_seconds);
        if (push_window > 0.0 && age > push_window) {
            continue;
        }
        double window_factor = 1.0;
        if (push_window > 0.0) {
            window_factor = std::max(0.0, 1.0 - age / push_window);
        }
        const double temporal = std::exp(-age / decay) * window_factor;
        const double weight = ev.intensity * reactive_intensity_ * temporal;
        if (weight <= 0.0) {
            continue;
        }
        for (std::size_t k = 0; k < total; ++k) {
            double px = xs_[k] - ex;
            double py = ys_[k] - ey;
            double dist2 = px * px + py * py;
            double spatial = std::exp(-dist2 / sigma2);
            double magnitude = base_disp * weight * spatial;
            if (magnitude <= 0.0) {
                continue;
            }
            double len = std::sqrt(dist2);
            if (len < 1e-5) {
                continue;
            }
            double dir_x = px / len;
            double dir_y = py / len;
            dx[k] += direction_sign * dir_x * magnitude;
            dy[k] += direction_sign * dir_y * magnitude;
        }
    }
}

}  // namespace kb::cfg
