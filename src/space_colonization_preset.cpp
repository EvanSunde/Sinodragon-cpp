#include "keyboard_configurator/space_colonization_preset.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <random>

namespace kb::cfg {

namespace {
    std::mt19937& rng()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        return gen;
    }

    double random01()
    {
        return std::uniform_real_distribution<double>(0.0, 1.0)(rng());
    }

    RgbColor parseHexColor(const std::string& value)
    {
        if (value.size() != 7 || value.front() != '#')
            return { 255, 255, 255 };
        auto hex = [](char ch) {
            if (ch >= '0' && ch <= '9')
                return ch - '0';
            char u = std::toupper(ch);
            return (u >= 'A' && u <= 'F') ? 10 + (u - 'A') : 0;
        };
        return {
            static_cast<uint8_t>((hex(value[1]) << 4) | hex(value[2])),
            static_cast<uint8_t>((hex(value[3]) << 4) | hex(value[4])),
            static_cast<uint8_t>((hex(value[5]) << 4) | hex(value[6]))
        };
    }

    inline double distSq(const Vector2& a, const Vector2& b)
    {
        return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
    }

    double distSqToSegment(const Vector2& p, const Vector2& v, const Vector2& w)
    {
        double l2 = distSq(v, w);
        if (l2 == 0.0)
            return distSq(p, v);
        double t = std::max(0.0, std::min(1.0, ((p.x - v.x) * (w.x - v.x) + (p.y - v.y) * (w.y - v.y)) / l2));
        Vector2 proj = { v.x + t * (w.x - v.x), v.y + t * (w.y - v.y) };
        return distSq(p, proj);
    }
}

std::string SpaceColonizationPreset::id() const { return "space_colonization"; }

void SpaceColonizationPreset::configure(const ParameterMap& params)
{
    auto setInt = [&](const char* k, int& v) { try { if(params.count(k)) v = std::stoi(params.at(k)); } catch(...) {} };
    auto setDbl = [&](const char* k, double& v) { try { if(params.count(k)) v = std::stod(params.at(k)); } catch(...) {} };

    setInt("attractors", attractor_count_);
    setDbl("kill_dist", kill_dist_);
    setDbl("influence_dist", influence_dist_);
    setDbl("segment_len", segment_len_);
    setDbl("thickness", thickness_base_);
    setDbl("growth_interval", growth_interval_);
    setDbl("lifespan", lifespan_);
    setDbl("fade_time", fade_time_);
    setDbl("thickness_decay", thickness_decay_);
    setDbl("trigger_proximity", trigger_proximity_);

    if (params.count("interaction_mode"))
        interaction_mode_ = params.at("interaction_mode");
    if (params.count("color_root"))
        color_root_ = parseHexColor(params.at("color_root"));
    if (params.count("color_tip"))
        color_tip_ = parseHexColor(params.at("color_tip"));

    if (params.count("reactive")) {
        std::string v = params.at("reactive");
        reactive_enabled_ = (v == "1" || v == "true" || v == "yes");
    }
    reset();
}

void SpaceColonizationPreset::reset()
{
    attractors_.clear();
    nodes_.clear();
    internal_time_ = 0;
    last_growth_time_ = 0;
    last_real_time_ = 0;

    if (!reactive_enabled_) {
        nodes_.push_back({ { random01(), random01() }, -1, thickness_base_, 0.0, 0.0, 1.0 });
        for (int i = 0; i < attractor_count_; ++i)
            attractors_.push_back({ random01(), random01() });
    } else if (interaction_mode_ == "food") {
        nodes_.push_back({ { random01(), random01() }, -1, thickness_base_, 0.0, 0.0, 1.0 });
    }
}

void SpaceColonizationPreset::applyKeyActivityInjection(double now)
{
    if (!reactive_enabled_ || !key_activity_provider_)
        return;

    auto events = key_activity_provider_->recentEvents(0.1);
    for (const auto& ev : events) {
        if (ev.key_index >= xs_.size())
            continue;

        double kx = xs_[ev.key_index];
        double ky = ys_[ev.key_index];

        if (interaction_mode_ == "root") {
            const double trig2 = trigger_proximity_ * trigger_proximity_;
            Node* refreshed = nullptr;
            for (auto& n : nodes_) {
                if (n.opacity > 0.05 && distSq(n.pos, { kx, ky }) < trig2) {
                    refreshed = &n;
                    break;
                }
            }

            if (refreshed) {
                refreshed->birth_time = now;
                refreshed->opacity = 1.0;
            } else {
                nodes_.push_back({ { kx, ky }, -1, thickness_base_, 0.0, now, 1.0 });
            }

            // Each press should continue feeding the branch so it keeps growing.
            for (int k = 0; k < 12; ++k) {
                double angle = random01() * 6.28;
                double dist = random01() * 0.15 + 0.02;
                attractors_.push_back({ kx + cos(angle) * dist, ky + sin(angle) * dist });
            }
        } else if (interaction_mode_ == "food") {
            for (int k = 0; k < 15; ++k) {
                double angle = random01() * 6.28;
                double dist = std::sqrt(random01()) * 0.22;
                attractors_.push_back({ kx + cos(angle) * dist, ky + sin(angle) * dist });
            }
        }
    }
}

void SpaceColonizationPreset::grow(double now)
{
    if (nodes_.empty() && attractors_.empty())
        return;
    if (nodes_.size() > 1200)
        return;

    std::vector<Vector2> node_forces(nodes_.size(), { 0, 0 });
    std::vector<int> node_counts(nodes_.size(), 0);
    std::vector<bool> attractor_active(attractors_.size(), true);

    double kill2 = kill_dist_ * kill_dist_;
    double inf2 = influence_dist_ * influence_dist_;

    for (size_t i = 0; i < attractors_.size(); ++i) {
        int closest_node = -1;
        double min_dist = 1e9;
        for (size_t n = 0; n < nodes_.size(); ++n) {
            if (nodes_[n].opacity <= 0.05)
                continue;
            double d2 = distSq(attractors_[i], nodes_[n].pos);
            if (d2 < kill2) {
                attractor_active[i] = false;
                closest_node = -1;
                break;
            }
            if (d2 < inf2 && d2 < min_dist) {
                min_dist = d2;
                closest_node = static_cast<int>(n);
            }
        }
        if (closest_node != -1 && attractor_active[i]) {
            Vector2 dir = { attractors_[i].x - nodes_[closest_node].pos.x, attractors_[i].y - nodes_[closest_node].pos.y };
            double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 0) {
                node_forces[closest_node].x += dir.x / len;
                node_forces[closest_node].y += dir.y / len;
                node_counts[closest_node]++;
            }
        }
    }

    size_t write_idx = 0;
    for (size_t i = 0; i < attractors_.size(); ++i) {
        if (attractor_active[i])
            attractors_[write_idx++] = attractors_[i];
    }
    attractors_.resize(write_idx);

    size_t current_count = nodes_.size();
    for (size_t i = 0; i < current_count; ++i) {
        if (node_counts[i] > 0) {
            Vector2 avg_dir = { node_forces[i].x / node_counts[i], node_forces[i].y / node_counts[i] };
            double len = std::sqrt(avg_dir.x * avg_dir.x + avg_dir.y * avg_dir.y);
            if (len > 0) {
                Vector2 new_pos = { nodes_[i].pos.x + (avg_dir.x / len) * segment_len_, nodes_[i].pos.y + (avg_dir.y / len) * segment_len_ };
                nodes_.push_back({ new_pos, (int)i, nodes_[i].thickness * thickness_decay_, nodes_[i].dist_from_root + 1.0, now, 1.0 });
            }
        }
    }
}

void SpaceColonizationPreset::render(const KeyboardModel& model, double time, KeyColorFrame& frame)
{
    if (!coords_built_)
        buildCoords(model);

    double real_dt = (last_real_time_ > 0) ? (time - last_real_time_) : 0.016;
    last_real_time_ = time;

    if (real_dt < 0.0)
        real_dt = 0.0;

    constexpr double overlay_pause_threshold = 0.5;
    if (real_dt > overlay_pause_threshold) {
        // Treat long gaps (shortcut overlay, window focus loss) as a pause so the forest continues afterward.
        real_dt = 0.0;
    }

    internal_time_ += real_dt;

    applyKeyActivityInjection(internal_time_);

    if (internal_time_ - last_growth_time_ > growth_interval_) {
        grow(internal_time_);
        last_growth_time_ = internal_time_;
    }

    // Performance/Crash fix: We calculate opacity and filter visible segments.
    struct ActiveSegment {
        size_t node_idx;
        int parent_idx;
        double minX, maxX, minY, maxY;
    };
    std::vector<ActiveSegment> visible;
    visible.reserve(nodes_.size());

    for (size_t n = 0; n < nodes_.size(); ++n) {
        auto& node = nodes_[n];
        if (interaction_mode_ == "food" && node.parent_idx == -1) {
            node.opacity = 1.0;
        } else {
            double age = internal_time_ - node.birth_time;
            node.opacity = (age < lifespan_) ? 1.0 : std::max(0.0, 1.0 - (age - lifespan_) / fade_time_);
        }

        // Safety: ensure parent_idx is valid for rendering a segment
        if (node.opacity > 0.01 && node.parent_idx >= 0 && (size_t)node.parent_idx < nodes_.size()) {
            const auto& p = nodes_[node.parent_idx].pos;
            const auto& c = node.pos;
            visible.push_back({ n, node.parent_idx, std::min(p.x, c.x) - 0.07, std::max(p.x, c.x) + 0.07, std::min(p.y, c.y) - 0.07, std::max(p.y, c.y) + 0.07 });
        }
    }

    size_t total = model.keyCount();
    if (frame.size() != total)
        frame.resize(total);

    // ACTUAL RENDERING LOOP
    for (size_t i = 0; i < total; ++i) {
        Vector2 kPos = { xs_[i], ys_[i] };
        double min_dist_sq = 1.0, best_opacity = 0.0, best_ratio = 0.0;
        bool found = false;

        for (const auto& seg : visible) {
            if (kPos.x < seg.minX || kPos.x > seg.maxX || kPos.y < seg.minY || kPos.y > seg.maxY)
                continue;

            // Re-verify safety before accessing
            const auto& node = nodes_[seg.node_idx];
            const auto& parent = nodes_[seg.parent_idx];

            double d2 = distSqToSegment(kPos, parent.pos, node.pos);
            if (d2 < 0.0028 && d2 < min_dist_sq) {
                min_dist_sq = d2;
                best_opacity = node.opacity;
                best_ratio = std::min(1.0, node.dist_from_root / 35.0);
                found = true;
            }
        }

        if (found) {
            double dist = std::sqrt(min_dist_sq);
            double brightness = std::exp(-dist * dist * 400.0) * best_opacity;
            frame.setColor(i, { (uint8_t)((color_root_.r * (1.0 - best_ratio) + color_tip_.r * best_ratio) * brightness), (uint8_t)((color_root_.g * (1.0 - best_ratio) + color_tip_.g * best_ratio) * brightness), (uint8_t)((color_root_.b * (1.0 - best_ratio) + color_tip_.b * best_ratio) * brightness) });
        } else {
            frame.setColor(i, { 0, 0, 0 });
        }
    }

    // CRITICAL: Cleanup MUST happen AFTER rendering is completely finished to avoid crashes.
    cleanupDeadNodes(internal_time_);
}

void SpaceColonizationPreset::cleanupDeadNodes(double now)
{
    if (nodes_.size() < 400)
        return;
    int alive_count = 0;
    for (const auto& n : nodes_)
        if (n.opacity > 0.05)
            alive_count++;

    if (alive_count < nodes_.size() * 0.1) {
        nodes_.clear();
        attractors_.clear();
        if (interaction_mode_ == "food")
            reset();
    }
}

void SpaceColonizationPreset::buildCoords(const KeyboardModel& model)
{
    const auto& layout = model.layout();
    size_t total = model.keyCount();
    xs_.resize(total);
    ys_.resize(total);
    double max_w = 0, max_h = (double)layout.size();
    for (const auto& r : layout)
        max_w = std::max(max_w, (double)r.size());
    size_t idx = 0;
    for (size_t r = 0; r < layout.size(); ++r) {
        for (size_t c = 0; c < layout[r].size(); ++c) {
            xs_[idx] = (max_w > 1) ? (double)c / (max_w - 1) : 0.5;
            ys_[idx] = (max_h > 1) ? (double)r / (max_h - 1) : 0.5;
            idx++;
        }
    }
    coords_built_ = true;
}

} // namespace kb::cfg
