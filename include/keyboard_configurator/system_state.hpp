#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace kb::cfg {

// Shared source of data-driven values for effects that show something rather
// than animate something: CPU and memory load, battery level, and whatever
// else is pushed in over the control socket.
//
// Sampled metrics are cached, because presets ask for them once per key per
// frame and /proc should not be read at that rate.
class SystemState {
public:
    // 0.0-1.0, the fraction of CPU time not spent idle since the last sample.
    [[nodiscard]] double cpuUsage();

    // 0.0-1.0 of physical memory in use (total minus available).
    [[nodiscard]] double memoryUsage();

    // 1-, 5- and 15-minute load average divided by the core count, capped at 1.
    [[nodiscard]] double loadAverage();

    // 0.0-1.0 charge. Returns false when the machine has no battery.
    bool battery(double& level, bool& charging);

    // Values pushed in by `sinoctl metric <name> <0..1>`.
    void setMetric(const std::string& name, double value);
    [[nodiscard]] double metric(const std::string& name, double fallback = 0.0) const;

    // Named states pushed in by `sinoctl state <name> <value>` -- what a CI
    // script uses to turn the keyboard red on a failed build.
    void setState(const std::string& name, const std::string& value);
    [[nodiscard]] std::string state(const std::string& name) const;

    // Seconds since the named state last changed, for effects that fade or
    // pulse after a transition.
    [[nodiscard]] double stateAge(const std::string& name) const;

private:
    using Clock = std::chrono::steady_clock;

    // True when the cached sample for `key` is stale enough to refresh.
    bool due(const std::string& key, std::chrono::milliseconds interval);

    mutable std::mutex mutex_;

    std::unordered_map<std::string, Clock::time_point> last_sampled_;
    std::unordered_map<std::string, double> metrics_;
    std::unordered_map<std::string, std::pair<std::string, Clock::time_point>> states_;

    double cpu_usage_{0.0};
    unsigned long long cpu_prev_total_{0};
    unsigned long long cpu_prev_idle_{0};

    double memory_usage_{0.0};
    double load_average_{0.0};
    double battery_level_{0.0};
    bool battery_charging_{false};
    bool battery_present_{false};
    bool battery_probed_{false};
};

using SystemStatePtr = std::shared_ptr<SystemState>;

}  // namespace kb::cfg
