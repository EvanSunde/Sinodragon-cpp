#include "keyboard_configurator/system_state.hpp"

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kb::cfg {

namespace {

constexpr auto kCpuInterval = std::chrono::milliseconds(500);
constexpr auto kMemoryInterval = std::chrono::milliseconds(1000);
constexpr auto kLoadInterval = std::chrono::milliseconds(2000);
constexpr auto kBatteryInterval = std::chrono::milliseconds(5000);

std::string readFirstLine(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return line;
}

}  // namespace

bool SystemState::due(const std::string& key, std::chrono::milliseconds interval) {
    const auto now = Clock::now();
    auto it = last_sampled_.find(key);
    if (it != last_sampled_.end() && now - it->second < interval) {
        return false;
    }
    last_sampled_[key] = now;
    return true;
}

double SystemState::cpuUsage() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!due("cpu", kCpuInterval)) {
        return cpu_usage_;
    }

    std::ifstream stat("/proc/stat");
    std::string label;
    if (!(stat >> label) || label != "cpu") {
        return cpu_usage_;
    }

    // user nice system idle iowait irq softirq steal ...
    unsigned long long values[8] = {0};
    for (auto& value : values) {
        stat >> value;
    }

    unsigned long long total = 0;
    for (auto value : values) {
        total += value;
    }
    const unsigned long long idle = values[3] + values[4];

    if (cpu_prev_total_ != 0 && total > cpu_prev_total_) {
        const double total_delta = static_cast<double>(total - cpu_prev_total_);
        const double idle_delta = static_cast<double>(idle - cpu_prev_idle_);
        cpu_usage_ = std::clamp(1.0 - idle_delta / total_delta, 0.0, 1.0);
    }
    cpu_prev_total_ = total;
    cpu_prev_idle_ = idle;
    return cpu_usage_;
}

double SystemState::memoryUsage() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!due("memory", kMemoryInterval)) {
        return memory_usage_;
    }

    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    double total = 0.0;
    double available = 0.0;

    while (std::getline(meminfo, line)) {
        std::istringstream parts(line);
        std::string key;
        double value = 0.0;
        parts >> key >> value;
        if (key == "MemTotal:") {
            total = value;
        } else if (key == "MemAvailable:") {
            available = value;
            break;  // MemAvailable always follows MemTotal
        }
    }

    if (total > 0.0) {
        memory_usage_ = std::clamp(1.0 - available / total, 0.0, 1.0);
    }
    return memory_usage_;
}

double SystemState::loadAverage() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!due("load", kLoadInterval)) {
        return load_average_;
    }

    std::ifstream loadavg("/proc/loadavg");
    double one_minute = 0.0;
    if (loadavg >> one_minute) {
        const long cores = std::max(1L, ::sysconf(_SC_NPROCESSORS_ONLN));
        load_average_ = std::clamp(one_minute / static_cast<double>(cores), 0.0, 1.0);
    }
    return load_average_;
}

bool SystemState::battery(double& level, bool& charging) {
    std::lock_guard<std::mutex> guard(mutex_);

    if (!battery_probed_ || due("battery", kBatteryInterval)) {
        battery_probed_ = true;
        battery_present_ = false;

        std::error_code ec;
        const std::filesystem::path base("/sys/class/power_supply");
        if (std::filesystem::exists(base, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind("BAT", 0) != 0) {
                    continue;
                }
                const std::string capacity = readFirstLine(entry.path() / "capacity");
                if (capacity.empty()) {
                    continue;
                }
                try {
                    battery_level_ = std::clamp(std::stod(capacity) / 100.0, 0.0, 1.0);
                } catch (...) {
                    continue;
                }
                const std::string status = readFirstLine(entry.path() / "status");
                battery_charging_ = (status == "Charging" || status == "Full");
                battery_present_ = true;
                break;
            }
        }
    }

    level = battery_level_;
    charging = battery_charging_;
    return battery_present_;
}

void SystemState::setMetric(const std::string& name, double value) {
    std::lock_guard<std::mutex> guard(mutex_);
    metrics_[name] = std::clamp(value, 0.0, 1.0);
}

double SystemState::metric(const std::string& name, double fallback) const {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = metrics_.find(name);
    return it == metrics_.end() ? fallback : it->second;
}

void SystemState::setState(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = states_.find(name);
    // Only restart the clock when the value actually changed, so a script that
    // reports "ok" every minute does not keep retriggering the transition.
    if (it != states_.end() && it->second.first == value) {
        return;
    }
    states_[name] = {value, Clock::now()};
}

std::string SystemState::state(const std::string& name) const {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = states_.find(name);
    return it == states_.end() ? std::string{} : it->second.first;
}

double SystemState::stateAge(const std::string& name) const {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = states_.find(name);
    if (it == states_.end()) {
        return 0.0;
    }
    return std::chrono::duration<double>(Clock::now() - it->second.second).count();
}

}  // namespace kb::cfg
