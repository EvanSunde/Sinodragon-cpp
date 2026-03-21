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

        if (nodes_.empty()) {
            // DYNAMIC ROOT: If the board is empty, this press is the seed.
            // Node: {pos}, parent, thickness, dist, birth, opacity, strength
            nodes_.push_back({ { kx, ky }, -1, thickness_base_, 0.0, now, 1.0, 2.0 });
        } else {
            // DYNAMIC TARGET: Add the exact key coordinate as an attractor.
            // We add 2-3 slightly offset points to give the vine a "cloud" to find.
            for (int i = 0; i < 3; ++i) {
                double offX = (random01() - 0.5) * 0.02;
                double offY = (random01() - 0.5) * 0.02;
                attractors_.push_back({ kx + offX, ky + offY });
            }
        }
    }
}

void SpaceColonizationPreset::grow(double now)
{
    if (attractors_.empty() || nodes_.empty()) return;
    if (nodes_.size() > 2000) return; 

    std::vector<Vector2> node_forces(nodes_.size(), { 0, 0 });
    std::vector<int> node_counts(nodes_.size(), 0);
    std::vector<bool> attractor_active(attractors_.size(), true);

    double kill2 = kill_dist_ * kill_dist_;
    double inf2 = influence_dist_ * influence_dist_;

    for (size_t i = 0; i < attractors_.size(); ++i) {
        int closest_node = -1;
        double min_dist = 1e9;

        for (size_t n = 0; n < nodes_.size(); ++n) {
            if (nodes_[n].opacity <= 0.05) continue;
            double d2 = distSq(attractors_[i], nodes_[n].pos);

            if (d2 < kill2) {
                // PATH FOUND! Reinforce the trail back to the root
                int current = static_cast<int>(n);
                while (current != -1) {
                    nodes_[current].strength += 0.4; // Make the path "bolder"
                    nodes_[current].birth_time = now; // Refresh the life of the path
                    current = nodes_[current].parent_idx;
                }
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
            node_forces[closest_node].x += dir.x / len;
            node_forces[closest_node].y += dir.y / len;
            node_counts[closest_node]++;
        }
    }

    // Remove "eaten" attractors
    size_t write_idx = 0;
    for (size_t i = 0; i < attractors_.size(); ++i) {
        if (attractor_active[i]) attractors_[write_idx++] = attractors_[i];
    }
    attractors_.resize(write_idx);

    // Create new segments
    size_t current_count = nodes_.size();
    for (size_t i = 0; i < current_count; ++i) {
        if (node_counts[i] > 0) {
            Vector2 avg_dir = { node_forces[i].x / node_counts[i], node_forces[i].y / node_counts[i] };
            double len = std::sqrt(avg_dir.x * avg_dir.x + avg_dir.y * avg_dir.y);
            
            Vector2 new_pos = { 
                nodes_[i].pos.x + (avg_dir.x / len) * segment_len_, 
                nodes_[i].pos.y + (avg_dir.y / len) * segment_len_ 
            };
            
            nodes_.push_back({ new_pos, (int)i, thickness_base_, nodes_[i].dist_from_root + 1, now, 1.0, 1.0 });
        }
    }
}

void SpaceColonizationPreset::render(const KeyboardModel& model, double time, KeyColorFrame& frame)
{
    if (!coords_built_) buildCoords(model);
    double real_dt = (last_real_time_ > 0) ? (time - last_real_time_) : 0.016;
    last_real_time_ = time;
    if (real_dt < 0.0 || real_dt > 0.5) real_dt = 0.0;
    internal_time_ += real_dt;

    applyKeyActivityInjection(internal_time_);

    if (internal_time_ - last_growth_time_ > growth_interval_) {
        grow(internal_time_);
        last_growth_time_ = internal_time_;
    }

    // Determine opacity/visibility
    struct ActiveSegment { size_t node_idx; int parent_idx; double minX, maxX, minY, maxY; };
    std::vector<ActiveSegment> visible;
    for (size_t n = 0; n < nodes_.size(); ++n) {
        double age = internal_time_ - nodes_[n].birth_time;
        nodes_[n].opacity = (age < lifespan_) ? 1.0 : std::max(0.0, 1.0 - (age - lifespan_) / fade_time_);
        
        if (nodes_[n].opacity > 0.01 && nodes_[n].parent_idx >= 0) {
            const auto& p = nodes_[nodes_[n].parent_idx].pos;
            const auto& c = nodes_[n].pos;
            visible.push_back({ n, nodes_[n].parent_idx, std::min(p.x, c.x)-0.1, std::max(p.x, c.x)+0.1, std::min(p.y, c.y)-0.1, std::max(p.y, c.y)+0.1 });
        }
    }

    size_t total = model.keyCount();
    if (frame.size() != total) frame.resize(total);

    for (size_t i = 0; i < total; ++i) {
        Vector2 kPos = { xs_[i], ys_[i] };
        double min_d2 = 1.0, best_opacity = 0.0, best_strength = 0.0, best_dist = 0.0;
        bool found = false;

        for (const auto& seg : visible) {
            if (kPos.x < seg.minX || kPos.x > seg.maxX || kPos.y < seg.minY || kPos.y > seg.maxY) continue;
            double d2 = distSqToSegment(kPos, nodes_[seg.parent_idx].pos, nodes_[seg.node_idx].pos);
            
            if (d2 < 0.005 && d2 < min_d2) {
                min_d2 = d2;
                best_opacity = nodes_[seg.node_idx].opacity;
                best_strength = nodes_[seg.node_idx].strength;
                best_dist = nodes_[seg.node_idx].dist_from_root;
                found = true;
            }
        }

        if (found) {
            double dist = std::sqrt(min_d2);
            // STRENGTH LOGIC: Higher strength = wider glow and higher brightness
            double capped_strength = std::min(4.0, best_strength);
            double glow_size = 400.0 / (1.0 + (capped_strength * 0.5));
            double brightness = std::exp(-dist * dist * glow_size) * best_opacity;
            
            // Dim base for new paths, bright for reinforced paths
            brightness *= (0.3 + (capped_strength / 4.0) * 0.7);

            double ratio = std::min(1.0, (best_dist * segment_len_) / 0.8);

            frame.setColor(i, { 
                (uint8_t)((color_root_.r * (1.0 - ratio) + color_tip_.r * ratio) * brightness),
                (uint8_t)((color_root_.g * (1.0 - ratio) + color_tip_.g * ratio) * brightness),
                (uint8_t)((color_root_.b * (1.0 - ratio) + color_tip_.b * ratio) * brightness) 
            });
        } else {
            frame.setColor(i, { 0, 0, 0 });
        }
    }
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
        reset();
    }
}

void SpaceColonizationPreset::buildCoords(const KeyboardModel& model)
{
    const auto& layout = model.layout();
    size_t total = model.keyCount();
    xs_.resize(total);
    ys_.resize(total);

    // 1. Calculate the actual grid dimensions
    double max_rows = static_cast<double>(layout.size());
    double max_cols = 0;
    for (const auto& r : layout) {
        max_cols = std::max(max_cols, static_cast<double>(r.size()));
    }

    // 2. Use a single scale factor to maintain a 1:1 aspect ratio.
    // This prevents the "stretched" look where growth moves faster horizontally.
    double scale = std::max(max_rows, max_cols);
    if (scale < 1.0)
        scale = 1.0;

    size_t idx = 0;
    for (size_t r = 0; r < layout.size(); ++r) {
        for (size_t c = 0; c < layout[r].size(); ++c) {
            // Mapping keys into a consistent coordinate space
            xs_[idx] = static_cast<double>(c) / scale;
            ys_[idx] = static_cast<double>(r) / scale;
            idx++;
        }
    }
    coords_built_ = true;
}
} // namespace kb::cfg
