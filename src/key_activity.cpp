#include "keyboard_configurator/key_activity.hpp"

#include <algorithm>

namespace kb::cfg {

KeyActivityProvider::KeyActivityProvider(std::size_t key_count,
                                         double history_window_seconds)
    : start_time_(std::chrono::steady_clock::now()),
      history_window_seconds_(history_window_seconds),
      key_count_(key_count) {}

void KeyActivityProvider::setKeyCount(std::size_t key_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    key_count_ = key_count;
    events_.clear();
}

void KeyActivityProvider::recordKeyPress(std::size_t key_index, double intensity) {
    if (key_index >= key_count_) {
        return;
    }
    const double t = nowSeconds();
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(Event{key_index, t, intensity});
    pruneStaleEvents(t);
}

std::vector<KeyActivityProvider::Event> KeyActivityProvider::recentEvents(double window_seconds) const {
    const double t = nowSeconds();
    const double window = std::clamp(window_seconds, 0.0, history_window_seconds_);
    std::lock_guard<std::mutex> lock(mutex_);
    pruneStaleEvents(t);
    std::vector<Event> out;
    const double cutoff = t - window;
    for (const auto& ev : events_) {
        if (ev.time_seconds >= cutoff) {
            out.push_back(ev);
        }
    }
    return out;
}

double KeyActivityProvider::nowSeconds() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - start_time_).count();
}

void KeyActivityProvider::pruneStaleEvents(double current_time) const {
    const double cutoff = current_time - history_window_seconds_;
    while (!events_.empty() && events_.front().time_seconds < cutoff) {
        events_.pop_front();
    }
}

}  // namespace kb::cfg
