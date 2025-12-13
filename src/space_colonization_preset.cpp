#include "keyboard_configurator/space_colonization_preset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace kb::cfg {

namespace {
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
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    double distSqToSegment(const Vector2& p, const Vector2& v, const Vector2& w)
    {
        double l2 = distSq(v, w);
        if (l2 == 0.0)
            return distSq(p, v);
        double t = ((p.x - v.x) * (w.x - v.x) + (p.y - v.y) * (w.y - v.y)) / l2;
        t = std::max(0.0, std::min(1.0, t));
        Vector2 projection = { v.x + t * (w.x - v.x), v.y + t * (w.y - v.y) };
        return distSq(p, projection);
    }
}

std::string SpaceColonizationPreset::id() const { return "space_colonization"; }

void SpaceColonizationPreset::configure(const ParameterMap& params)
{
    auto setInt = [&](const char* k, int& v) { if(params.count(k)) v = std::stoi(params.at(k)); };
    auto setDbl = [&](const char* k, double& v) { if(params.count(k)) v = std::stod(params.at(k)); };

    setInt("attractors", attractor_count_);
    setDbl("kill_dist", kill_dist_);
    setDbl("influence_dist", influence_dist_);
    setDbl("segment_len", segment_len_);
    setDbl("zoom", zoom_);
    setDbl("thickness", thickness_base_);

    // NEW PARAMS
    setDbl("growth_interval", growth_interval_); // Speed control
    setDbl("lifespan", lifespan_); // Time before fade
    setDbl("fade_time", fade_time_); // Duration of fade

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
    // We do NOT spawn a default root if reactive is enabled.
    // We wait for a keypress to spawn a root.
    if (!reactive_enabled_) {
        nodes_.push_back({ { 0.5, 0.5 }, -1, thickness_base_, 0.0, 0.0, 1.0 });
        // Initial random attractors for static mode
        for (int i = 0; i < attractor_count_; ++i) {
            double x = (double)rand() / RAND_MAX;
            double y = (double)rand() / RAND_MAX;
            attractors_.push_back({ x, y });
        }
    }
}

void SpaceColonizationPreset::grow(double now)
{
    if (nodes_.empty() && attractors_.empty())
        return;

    // Standard Space Colonization Algorithm
    std::vector<Vector2> node_forces(nodes_.size(), { 0, 0 });
    std::vector<int> node_counts(nodes_.size(), 0);
    std::vector<bool> attractor_active(attractors_.size(), true);

    double kill2 = kill_dist_ * kill_dist_;
    double inf2 = influence_dist_ * influence_dist_;

    // 1. Attractors exert force on closest node
    for (size_t i = 0; i < attractors_.size(); ++i) {
        int closest_node = -1;
        double min_dist = 1e9;

        // Optimization: Only check active nodes (opacity > 0)
        // Note: Checking ALL nodes is O(N*M). For <2000 items it's fine.
        for (size_t n = 0; n < nodes_.size(); ++n) {
            if (nodes_[n].opacity <= 0.01)
                continue; // Dead nodes don't grow

            double d2 = distSq(attractors_[i], nodes_[n].pos);
            if (d2 < kill2) {
                attractor_active[i] = false;
                closest_node = -1;
                break; // Eaten
            }
            if (d2 < inf2 && d2 < min_dist) {
                min_dist = d2;
                closest_node = static_cast<int>(n);
            }
        }

        if (closest_node != -1 && attractor_active[i]) {
            Vector2 dir = { attractors_[i].x - nodes_[closest_node].pos.x,
                attractors_[i].y - nodes_[closest_node].pos.y };
            // Normalize
            double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 0) {
                node_forces[closest_node].x += dir.x / len;
                node_forces[closest_node].y += dir.y / len;
                node_counts[closest_node]++;
            }
        }
    }

    // Remove eaten attractors
    size_t write_idx = 0;
    for (size_t i = 0; i < attractors_.size(); ++i) {
        if (attractor_active[i])
            attractors_[write_idx++] = attractors_[i];
    }
    attractors_.resize(write_idx);

    // 2. Spawn new segments
    // Capture size BEFORE loop to avoid infinite growth in one frame
    size_t current_count = nodes_.size();
    for (size_t i = 0; i < current_count; ++i) {
        if (node_counts[i] > 0) {
            Vector2 avg_dir = { node_forces[i].x / node_counts[i], node_forces[i].y / node_counts[i] };
            double len = std::sqrt(avg_dir.x * avg_dir.x + avg_dir.y * avg_dir.y);
            if (len > 0) {
                avg_dir.x /= len;
                avg_dir.y /= len;

                Vector2 new_pos = {
                    nodes_[i].pos.x + avg_dir.x * segment_len_,
                    nodes_[i].pos.y + avg_dir.y * segment_len_
                };

                Node new_node;
                new_node.pos = new_pos;
                new_node.parent_idx = static_cast<int>(i);
                new_node.thickness = nodes_[i].thickness * thickness_decay_;
                if (new_node.thickness < 0.005)
                    new_node.thickness = 0.005;
                new_node.dist_from_root = nodes_[i].dist_from_root + 1.0;
                new_node.birth_time = now; // <--- Set birth time
                new_node.opacity = 1.0;

                nodes_.push_back(new_node);
            }
        }
    }
}

void SpaceColonizationPreset::buildCoords(const KeyboardModel& model)
{
    const auto& layout = model.layout();
    size_t total = model.keyCount();
    xs_.resize(total);
    ys_.resize(total);

    double max_w = 0, max_h = layout.size();
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

void SpaceColonizationPreset::applyKeyActivityInjection(double now)
{
    if (!reactive_enabled_ || !key_activity_provider_)
        return;

    auto events = key_activity_provider_->recentEvents(0.1); // check last 100ms
    for (const auto& ev : events) {
        if (ev.key_index >= xs_.size())
            continue;

        // Probability check to avoid spamming roots on every micro-frame of a keypress
        // ev.intensity usually 0.0 to 1.0.
        if ((rand() % 100) < 30) { // 30% chance per event to spawn a root

            // 1. SPAWN A NEW ROOT at the key location
            Node root;
            root.pos = { xs_[ev.key_index], ys_[ev.key_index] };
            root.parent_idx = -1;
            root.thickness = thickness_base_;
            root.birth_time = now;
            root.opacity = 1.0;
            nodes_.push_back(root);

            // 2. SPAWN FOOD around this key so the root has something to eat
            // We scatter food in a radius around the key
            int food_to_spawn = 20;
            double spread = 0.2; // 20% of keyboard width

            for (int k = 0; k < food_to_spawn; ++k) {
                double angle = (double)rand() / RAND_MAX * 6.28;
                double dist = (double)rand() / RAND_MAX * spread + 0.02; // Min dist 0.02

                double fx = root.pos.x + cos(angle) * dist;
                double fy = root.pos.y + sin(angle) * dist;

                // Bounds check
                if (fx >= 0 && fx <= 1.0 && fy >= 0 && fy <= 1.0) {
                    attractors_.push_back({ fx, fy });
                }
            }
        }
    }
}

void SpaceColonizationPreset::cleanupDeadNodes(double now)
{
    // Only run cleanup occasionally or if array gets too big
    if (nodes_.size() < 1000)
        return;

    // We can't easily remove nodes from the middle of the vector because
    // children reference parents by INDEX.
    // If we delete index 5, index 6 becomes 5, breaking the parent_idx of any child pointing to 6.

    // SIMPLE SOLUTION: Just clear everything if it gets too huge.
    // COMPLEX SOLUTION: Re-indexing. (Too slow for this context).
    // PRACTICAL SOLUTION: If the *Roots* are dead, and their children are dead, we can clear.

    // For now, to prevent memory leaks in a long running process:
    // If we have MANY nodes and most are invisible, hard reset.
    // A proper solution would use a linked list or ID-based referencing,
    // but Vector+Index is required for cache coherency and speed here.

    int visible_count = 0;
    for (const auto& n : nodes_) {
        if (n.opacity > 0.05)
            visible_count++;
    }

    // If 90% of nodes are invisible, clear the buffer to start fresh
    if (nodes_.size() > 500 && visible_count < nodes_.size() * 0.1) {
        nodes_.clear();
        attractors_.clear(); // Clear unused food too
    }
}

void SpaceColonizationPreset::render(const KeyboardModel& model, double time, KeyColorFrame& frame)
{
    if (!coords_built_)
        buildCoords(model);

    // 1. Input processing
    applyKeyActivityInjection(time);

    // 2. Growth Logic (Throttled)
    if (time - last_growth_time_ > growth_interval_) {
        grow(time);
        last_growth_time_ = time;
    }

    // 3. Update Opacity & Cleanup
    for (auto& node : nodes_) {
        double age = time - node.birth_time;
        if (age < lifespan_) {
            node.opacity = 1.0;
        } else {
            double faded = age - lifespan_;
            node.opacity = 1.0 - (faded / fade_time_);
            if (node.opacity < 0)
                node.opacity = 0;
        }
    }
    cleanupDeadNodes(time);

    // 4. Render
    size_t total = model.keyCount();
    if (frame.size() != total)
        frame.resize(total);

    for (size_t i = 0; i < total; ++i) {
        Vector2 kPos = { xs_[i], ys_[i] };

        double min_dist_sq = 1000.0;
        double best_opacity = 0.0;
        double best_ratio = 0.0;

        // Check distance to all VISIBLE segments
        for (size_t n = 0; n < nodes_.size(); ++n) {
            const Node& node = nodes_[n];
            if (node.opacity <= 0.01)
                continue; // Skip invisible
            if (node.parent_idx == -1)
                continue; // Skip roots (dots)

            const Node& parent = nodes_[node.parent_idx];

            double d2 = distSqToSegment(kPos, parent.pos, node.pos);

            // "Glow" radius check
            // We want to blend overlapping veins?
            // Simple max blending for now.

            // Effective thickness
            double thick = node.thickness * 1.5; // Visual multiplier
            if (d2 < min_dist_sq) {
                // If we are closer to this vein than any other
                min_dist_sq = d2;
                best_opacity = node.opacity;
                best_ratio = std::min(1.0, node.dist_from_root / 30.0);
            }
        }

        // Calculate final color
        double dist = std::sqrt(min_dist_sq);
        double core = 0.015; // Width of the core line

        double brightness = 0.0;
        if (dist < core) {
            brightness = 1.0;
        } else {
            // Soft glow falloff
            brightness = std::max(0.0, 1.0 - (dist - core) * 15.0);
        }

        // Apply fading opacity
        brightness *= best_opacity;

        if (brightness > 0.01) {
            RgbColor c;
            // Gradient from Root Color to Tip Color
            c.r = (uint8_t)(color_root_.r * (1.0 - best_ratio) + color_tip_.r * best_ratio);
            c.g = (uint8_t)(color_root_.g * (1.0 - best_ratio) + color_tip_.g * best_ratio);
            c.b = (uint8_t)(color_root_.b * (1.0 - best_ratio) + color_tip_.b * best_ratio);

            c.r = (uint8_t)(c.r * brightness);
            c.g = (uint8_t)(c.g * brightness);
            c.b = (uint8_t)(c.b * brightness);

            frame.setColor(i, c);
        } else {
            frame.setColor(i, { 0, 0, 0 });
        }
    }
}

} // namespace kb::cfg
