#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace kb::cfg {

class KeyActivityProvider {
public:
    struct Event {
        std::size_t key_index{0};
        double time_seconds{0.0};
        double intensity{1.0};
    };

    KeyActivityProvider(std::size_t key_count = 0u,
                        double history_window_seconds = 2.5);

    void setKeyCount(std::size_t key_count);

    void recordKeyPress(std::size_t key_index, double intensity = 1.0);

    [[nodiscard]] std::vector<Event> recentEvents(double window_seconds) const;

    [[nodiscard]] double nowSeconds() const;

private:
    const std::chrono::steady_clock::time_point start_time_;
    double history_window_seconds_;
    std::size_t key_count_;

    mutable std::mutex mutex_;
    mutable std::deque<Event> events_;

    void pruneStaleEvents(double current_time) const;
};

using KeyActivityProviderPtr = std::shared_ptr<KeyActivityProvider>;

}  // namespace kb::cfg
