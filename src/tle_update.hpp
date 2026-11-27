#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "net.hpp"

class TleUpdater {
public:
    ~TleUpdater() { quit_ = true; if (worker_.joinable()) worker_.detach(); }

    // Checks at start-up and then every 30 minutes; files younger than 12 hours are left alone, so this is cheap.
    void start(const std::string& directory) {
        directory_ = directory;
        worker_ = std::thread([this] {
            while (!quit_) {
                refreshStaleFiles();
                for (int i = 0; i < 1800 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }

    // True once after new element sets have been downloaded, so the app can reload them.
    bool takeUpdated() { return updated_.exchange(false); }

    std::string status() {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

private:
    struct Source { const char* fileName; const char* group; std::uintmax_t minimumBytes; };

    static bool isYoungerThan(const std::string& path, std::chrono::hours limit) {
        namespace fs = std::filesystem;
        std::error_code ignored;
        if (!fs::exists(path, ignored)) return false;
        return fs::file_time_type::clock::now() - fs::last_write_time(path, ignored) < limit;
    }

    static void recordAttempt(const std::string& path) {
        std::ofstream(path + ".attempt") << "attempted\n";
    }

    void refreshStaleFiles() {
        namespace fs = std::filesystem;
        std::error_code ignoredError;
        fs::create_directories(directory_, ignoredError);
        const Source sources[] = { { "stations.txt", "stations", 300 }, { "active.txt", "active", 100000 } };
        bool updated = false;
        for (const Source& source : sources) {
            const std::string path = directory_ + source.fileName;
            if (isYoungerThan(path, std::chrono::hours(12))) continue;
            if (isYoungerThan(path + ".attempt", std::chrono::hours(3))) continue;
            recordAttempt(path);
            const std::string url = std::string("https://celestrak.org/NORAD/elements/gp.php?GROUP=") + source.group + "&FORMAT=tle";
            const std::string download = path + ".new";
            std::error_code ignored;
            if (curlGet(url, download, 60) && fs::file_size(download, ignored) >= source.minimumBytes) {
                fs::remove(path, ignored);
                fs::rename(download, path, ignored);
                updated = true;
            } else {
                fs::remove(download, ignored);
            }
        }
        if (updated) { setStatus("Satellite elements refreshed"); updated_ = true; }
    }

    void setStatus(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = text;
    }

    std::string directory_, status_;
    std::atomic<bool> quit_{ false }, updated_{ false };
    std::mutex mutex_;
    std::thread worker_;
};
