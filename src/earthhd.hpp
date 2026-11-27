#pragma once
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net.hpp"
#include "raylib.h"

class EarthImagery {
public:
    ~EarthImagery() { if (th_.joinable()) th_.detach(); }
    void start(const std::string& dir) {
        dir_ = dir;
        dayPath_ = dir + "earth_hd.jpg";
        nightPath_ = dir + "earth_night.jpg";
        th_ = std::thread([this] { run(); });
    }
    bool dayReady() const { return dayReady_; }
    bool nightReady() const { return nightReady_; }
    const std::string& dayPath() const { return dayPath_; }
    const std::string& nightPath() const { return nightPath_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }

private:

    bool build(const std::string& layer, const std::string& timePart, const std::string& res, int level, const char* ext, const std::string& out, const std::string& label) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists(out, ec) && fs::file_size(out, ec) > 100000) return true;
        const double tileDeg = 288.0 / (1 << level);
        const int cols = (int)std::ceil(360.0 / tileDeg - 1e-9), rows = (int)std::ceil(180.0 / tileDeg - 1e-9);
        const std::string tdir = dir_ + "tiles/hd/" + label + "/";
        std::atomic<int> next{ 0 }, failed{ 0 }, done{ 0 };
        auto work = [&] {
            for (;;) {
                const int i = next++;
                if (i >= cols * rows) return;
                const int r = i / cols, c = i % cols;
                const std::string path = tdir + std::to_string(r) + "_" + std::to_string(c) + ext;
                std::error_code e2;
                if (fs::exists(path, e2) && fs::file_size(path, e2) > 0) { ++done; continue; }
                const std::string url = "https://gibs.earthdata.nasa.gov/wmts/epsg4326/best/" + layer + "/default/" + timePart + res + "/" + std::to_string(level) + "/" + std::to_string(r) + "/" + std::to_string(c) + ext;
                bool ok = false;
                for (int attempt = 0; attempt < 3 && !ok; ++attempt) ok = curlGet(url, path, 25);
                if (ok) ++done; else ++failed;
                setStatus("Earth imagery: " + label + " " + std::to_string(done.load()) + "/" + std::to_string(cols * rows));
            }
        };
        std::vector<std::thread> pool;
        for (int i = 0; i < 4; ++i) pool.emplace_back(work);
        for (auto& t : pool) t.join();
        if (failed > 0) { setStatus("Earth imagery: some tiles failed (will retry next start)"); return false; }
        Image big = GenImageColor(cols * 512, rows * 512, BLACK);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                Image t = LoadImage((tdir + std::to_string(r) + "_" + std::to_string(c) + ext).c_str());
                if (!t.data) continue;
                ImageDraw(&big, t, { 0, 0, (float)t.width, (float)t.height }, { (float)(c * 512), (float)(r * 512), 512, 512 }, WHITE);
                UnloadImage(t);
            }
        const bool ok = ExportImage(big, out.c_str());
        UnloadImage(big);
        return ok;
    }
    void run() {

        if (build("VIIRS_Black_Marble", "2016-01-01/", "500m", 3, ".png", nightPath_, "night")) nightReady_ = true;
        if (build("BlueMarble_ShadedRelief_Bathymetry", "", "500m", 3, ".jpeg", dayPath_, "day")) dayReady_ = true;
        if (dayReady_ && nightReady_) setStatus("");
    }
    void setStatus(const std::string& s) { std::lock_guard<std::mutex> lk(m_); status_ = s; }
    std::string dir_, dayPath_, nightPath_;
    std::string status_;
    std::mutex m_;
    std::atomic<bool> dayReady_{ false }, nightReady_{ false };
    std::thread th_;
};
