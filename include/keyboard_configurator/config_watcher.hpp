#pragma once

#include <filesystem>
#include <chrono>
#include <atomic>
#include <iostream>

namespace kb::cfg {

/**
 * Watches a config file for modifications.
 * Tracks file modification time and detects when it changes.
 */
class ConfigWatcher {
public:
    ConfigWatcher(const std::string& config_path)
        : config_path_(config_path),
          change_detected_(false) {
        updateLastModified();
    }

    /**
     * Check if the config file has been modified since last check.
     * @return true if file was modified, false otherwise
     */
    bool hasChanged() {
        std::filesystem::file_time_type current_modified{};
        if (!getLastModified(current_modified)) {
            // File doesn't exist or can't be accessed
            return false;
        }

        if (current_modified > last_modified_) {
            last_modified_ = current_modified;
            change_detected_ = true;
            std::cout << "[ConfigWatcher] Config file changed: " << config_path_ << '\n';
            return true;
        }

        return false;
    }

    /**
     * Reset change detection flag without updating modification time.
     * Useful after handling a change.
     */
    void clearChangeFlag() {
        change_detected_ = false;
    }

    /**
     * Manually update the tracked modification time.
     */
    void updateLastModified() {
        if (!getLastModified(last_modified_)) {
            last_modified_ = std::filesystem::file_time_type::min();
        }
    }

    /**
     * Get the config file path being watched.
     */
    const std::string& getPath() const {
        return config_path_;
    }

    /**
     * Check if change was detected in the last check.
     */
    bool wasChangeDetected() const {
        return change_detected_;
    }

private:
    // Compare raw file_time_type values. Converting them into system_clock
    // (last_write - file_clock::now() + system_clock::now()) re-reads both
    // clocks on every poll, so the result drifts by however much the two
    // clocks disagree between calls -- which made this report a change on an
    // untouched file, silently reloading the config and killing any running
    // game or profile hold.
    bool getLastModified(std::filesystem::file_time_type& out) const {
        std::error_code ec;
        if (!std::filesystem::exists(config_path_, ec) || ec) {
            return false;
        }
        out = std::filesystem::last_write_time(config_path_, ec);
        if (ec) {
            std::cerr << "[ConfigWatcher] Cannot stat " << config_path_ << ": " << ec.message()
                      << '\n';
            return false;
        }
        return true;
    }

    std::string config_path_;
    std::filesystem::file_time_type last_modified_{};
    std::atomic<bool> change_detected_;
};

}  // namespace kb::cfg
