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
          last_modified_(0),
          change_detected_(false) {
        updateLastModified();
    }

    /**
     * Check if the config file has been modified since last check.
     * @return true if file was modified, false otherwise
     */
    bool hasChanged() {
        auto current_modified = getLastModified();
        
        if (current_modified == 0) {
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
        last_modified_ = getLastModified();
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
    using timestamp_t = std::chrono::system_clock::time_point::rep;

    timestamp_t getLastModified() const {
        try {
            if (!std::filesystem::exists(config_path_)) {
                return 0;
            }
            auto last_write = std::filesystem::last_write_time(config_path_);
            // Convert to duration since epoch
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                last_write - std::filesystem::file_time_type::clock::now() + 
                std::chrono::system_clock::now()
            );
            return sctp.time_since_epoch().count();
        } catch (const std::exception& e) {
            std::cerr << "[ConfigWatcher] Error checking file modification: " << e.what() << '\n';
            return 0;
        }
    }

    std::string config_path_;
    timestamp_t last_modified_;
    std::atomic<bool> change_detected_;
};

}  // namespace kb::cfg
