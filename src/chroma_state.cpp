#include "keyboard_configurator/chroma_state.hpp"

namespace kb::cfg {

std::uint64_t ChromaState::setGrid(const Grid& grid) {
    std::lock_guard<std::mutex> guard(mutex_);
    grid_ = grid;
    return ++sequence_;
}

std::uint64_t ChromaState::grid(Grid& out) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!enabled_ || sequence_ == 0) {
        return 0;
    }
    out = grid_;
    return sequence_;
}

void ChromaState::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> guard(mutex_);
    enabled_ = enabled;
}

bool ChromaState::enabled() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return enabled_;
}

void ChromaState::clear() {
    std::lock_guard<std::mutex> guard(mutex_);
    grid_.fill(0);
    sequence_ = 0;
}

void ChromaState::setListening(int port) {
    std::lock_guard<std::mutex> guard(mutex_);
    status_.listening = true;
    status_.port = port;
    status_.listen_error.clear();
}

void ChromaState::setListenError(int port, const std::string& error) {
    std::lock_guard<std::mutex> guard(mutex_);
    status_.listening = false;
    status_.port = port;
    status_.listen_error = error;
}

void ChromaState::setSessions(std::size_t count) {
    std::lock_guard<std::mutex> guard(mutex_);
    status_.sessions = count;
}

void ChromaState::noteFrameFrom(const std::string& app) {
    std::lock_guard<std::mutex> guard(mutex_);
    status_.app = app;
    ++status_.frames;
    status_.last_frame = std::chrono::steady_clock::now();
}

ChromaState::Status ChromaState::status() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return status_;
}

}  // namespace kb::cfg
