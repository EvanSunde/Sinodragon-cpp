#pragma once

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace kb::cfg {

/**
 * Exponential backoff retry helper for device connection attempts.
 * Retries with exponentially increasing delays until success or max attempts reached.
 */
class RetryHelper {
public:
    struct Config {
        int max_attempts;
        int initial_delay_ms;
        int max_delay_ms;
        double backoff_multiplier;

        Config() 
            : max_attempts(10),
              initial_delay_ms(500),
              max_delay_ms(30000),
              backoff_multiplier(2.0) {}

        Config(int max_att, int init_delay, int max_delay, double multiplier)
            : max_attempts(max_att),
              initial_delay_ms(init_delay),
              max_delay_ms(max_delay),
              backoff_multiplier(multiplier) {}
    };

    RetryHelper(const Config& config = Config()) : config_(config) {}

    /**
     * Execute a function with exponential backoff retry.
     * 
     * @param func A callable that returns true on success, false on failure
     * @param description A description for logging purposes
     * @return true if function succeeded, false if all retries exhausted
     */
    template <typename Func>
    bool executeWithRetry(Func func, const std::string& description) {
        for (int attempt = 1; attempt <= config_.max_attempts; ++attempt) {
            if (func()) {
                if (attempt > 1) {
                    std::cout << "[RetryHelper] " << description << " succeeded on attempt " 
                              << attempt << "/" << config_.max_attempts << '\n';
                }
                return true;
            }

            if (attempt < config_.max_attempts) {
                int delay_ms = calculateDelay(attempt - 1);
                std::cerr << "[RetryHelper] " << description << " failed (attempt " 
                          << attempt << "/" << config_.max_attempts 
                          << "), retrying in " << delay_ms << "ms...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            } else {
                std::cerr << "[RetryHelper] " << description << " failed after " 
                          << config_.max_attempts << " attempts\n";
            }
        }
        return false;
    }

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    int calculateDelay(int attempt_index) const {
        // Calculate exponential backoff: initial_delay * (multiplier ^ attempt_index)
        double delay = config_.initial_delay_ms * std::pow(config_.backoff_multiplier, attempt_index);
        
        // Cap at max delay
        if (delay > config_.max_delay_ms) {
            delay = config_.max_delay_ms;
        }
        
        return static_cast<int>(delay);
    }

    Config config_;
};

/**
 * Non-blocking counterpart to RetryHelper, for callers that cannot afford to
 * sleep -- the render thread, for instance, must keep ticking while the device
 * is away. Shares the same backoff policy: ask ready() whether it is time for
 * the next attempt, then report the outcome.
 */
class Backoff {
public:
    using Clock = std::chrono::steady_clock;

    explicit Backoff(const RetryHelper::Config& config = RetryHelper::Config())
        : config_(config) {}

    [[nodiscard]] bool ready(Clock::time_point now = Clock::now()) const {
        return now >= next_attempt_;
    }

    void recordFailure(Clock::time_point now = Clock::now()) {
        double delay = config_.initial_delay_ms * std::pow(config_.backoff_multiplier, attempt_);
        if (delay > config_.max_delay_ms) {
            delay = config_.max_delay_ms;
        }
        current_delay_ = std::chrono::milliseconds(static_cast<int>(delay));
        next_attempt_ = now + current_delay_;
        ++attempt_;
    }

    void reset() {
        attempt_ = 0;
        current_delay_ = std::chrono::milliseconds(0);
        next_attempt_ = Clock::time_point{};
    }

    [[nodiscard]] std::chrono::milliseconds currentDelay() const noexcept { return current_delay_; }
    [[nodiscard]] int attempts() const noexcept { return attempt_; }

private:
    RetryHelper::Config config_;
    int attempt_{0};
    std::chrono::milliseconds current_delay_{0};
    Clock::time_point next_attempt_{};
};

}  // namespace kb::cfg
