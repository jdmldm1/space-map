#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ais.hpp"
#include "net.hpp"
#include "time_utils.hpp"

struct RadarFrame { double time = 0; std::string path; };

class RadarFeed {
public:
    explicit RadarFeed(std::string tmpFile) : tmp_(std::move(tmpFile)) {}
    ~RadarFeed() { quit_ = true; if (th_.joinable()) th_.detach(); }
    void start() { th_ = std::thread([this] { loop(); }); }
    void setEnabled(bool on) { on_ = on; if (on && next_ > steadySeconds() + 5) next_ = 0; }
    unsigned version() const { return version_; }
    std::string host() { std::lock_guard<std::mutex> lk(m_); return host_; }
    std::vector<RadarFrame> frames() { std::lock_guard<std::mutex> lk(m_); return frames_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }

    static std::vector<RadarFrame> parse(const std::string& s, std::string& host) {
        std::vector<RadarFrame> out;
        host = ais::jraw(s, "host");
        const size_t r = s.find("\"radar\"");
        if (r == std::string::npos) return out;
        const size_t past = s.find("\"past\"", r);
        for (const std::string& o : ais::objectsOf(s, past == std::string::npos ? r : past)) {
            RadarFrame f;
            double t = 0;
            if (!ais::jnum(o, "time", t)) continue;
            f.time = t; f.path = ais::jraw(o, "path");
            if (!f.path.empty()) out.push_back(f);
            if (out.size() >= 24) break;
        }
        return out;
    }

private:
    void loop() {
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const double now = steadySeconds();
            if (!on_ || now < next_) continue;
            if (curlGet("https://api.rainviewer.com/public/weather-maps.json", tmp_, 12)) {
                std::string h;
                auto f = parse(readFile(tmp_), h);
                std::lock_guard<std::mutex> lk(m_);
                if (!f.empty()) { frames_ = f; host_ = h; status_ = "radar: " + std::to_string(f.size()) + " frames (RainViewer)"; ++version_; }
                next_ = now + 300.0;
            } else {
                std::lock_guard<std::mutex> lk(m_);
                status_ = "radar: RainViewer unavailable";
                next_ = now + 60.0;
            }
        }
    }
    std::string tmp_, host_, status_ = "radar: loading...";
    std::mutex m_;
    std::thread th_;
    std::atomic<bool> on_{ false }, quit_{ false };
    std::atomic<unsigned> version_{ 0 };
    std::atomic<double> next_{ 0 };
    std::vector<RadarFrame> frames_;
};

struct WeatherPoint {
    double lat = 0, lon = 0, temp = 0, feels = 0, humidity = 0, precip = 0, cloud = 0, pressure = 0, windKt = 0, windDir = 0, gustKt = 0;
    int code = 0;
    bool ok = false;
};

inline const char* weatherCodeName(int c) {
    switch (c) {
        case 0: return "Clear sky"; case 1: return "Mainly clear"; case 2: return "Partly cloudy"; case 3: return "Overcast";
        case 45: case 48: return "Fog"; case 51: case 53: case 55: return "Drizzle"; case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain"; case 63: return "Rain"; case 65: return "Heavy rain"; case 66: case 67: return "Freezing rain";
        case 71: return "Light snow"; case 73: return "Snow"; case 75: return "Heavy snow"; case 77: return "Snow grains";
        case 80: return "Rain showers"; case 81: case 82: return "Heavy showers"; case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm"; case 96: case 99: return "Thunderstorm, hail";
        default: return "";
    }
}

struct WeatherRequest { bool valid = false; double cLat = 0, cLon = 0, minLat = 0, maxLat = 0, minLon = 0, maxLon = 0; bool wantGrid = false; };

class WeatherFeed {
public:
    explicit WeatherFeed(std::string tmpDir) : tmp_(std::move(tmpDir)) {}
    ~WeatherFeed() { quit_ = true; if (th_.joinable()) th_.detach(); }
    void start() { th_ = std::thread([this] { loop(); }); }
    void setEnabled(bool on) { on_ = on; }
    void setRequest(const WeatherRequest& r) { std::lock_guard<std::mutex> lk(m_); req_ = r; }
    unsigned version() const { return version_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }
    WeatherPoint center() { std::lock_guard<std::mutex> lk(m_); return center_; }
    std::vector<WeatherPoint> grid() { std::lock_guard<std::mutex> lk(m_); return grid_; }

    static std::vector<WeatherPoint> parse(const std::string& s) {
        std::vector<WeatherPoint> out;
        std::vector<std::string> objs;
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n')) ++i;
        if (i < s.size() && s[i] == '[') objs = ais::objectsOf(s, i);
        else objs.push_back(s);
        for (const std::string& o : objs) {
            WeatherPoint w;
            double d;
            const size_t c = o.find("\"current\"");
            if (c == std::string::npos) continue;
            const std::string cur = o.substr(c);
            if (ais::jnum(o, "latitude", d)) w.lat = d; else continue;
            if (ais::jnum(o, "longitude", d)) w.lon = d;
            if (ais::jnum(cur, "temperature_2m", d)) w.temp = d;
            if (ais::jnum(cur, "apparent_temperature", d)) w.feels = d;
            if (ais::jnum(cur, "relative_humidity_2m", d)) w.humidity = d;
            if (ais::jnum(cur, "precipitation", d)) w.precip = d;
            if (ais::jnum(cur, "cloud_cover", d)) w.cloud = d;
            if (ais::jnum(cur, "pressure_msl", d)) w.pressure = d;
            if (ais::jnum(cur, "wind_speed_10m", d)) w.windKt = d;
            if (ais::jnum(cur, "wind_direction_10m", d)) w.windDir = d;
            if (ais::jnum(cur, "wind_gusts_10m", d)) w.gustKt = d;
            if (ais::jnum(cur, "weather_code", d)) w.code = (int)d;
            w.ok = true;
            out.push_back(w);
        }
        return out;
    }

private:
    void loop() {
        double lastC = 0, lastG = 0, lastLat = 999, lastLon = 999, lastGLat = 999, lastGLon = 999;
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            if (!on_) continue;
            WeatherRequest r;
            { std::lock_guard<std::mutex> lk(m_); r = req_; }
            if (!r.valid) continue;
            const double now = steadySeconds();
            const char* cur = "current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,weather_code&wind_speed_unit=kn";

            if (now - lastC > 4.0 && (std::fabs(r.cLat - lastLat) + std::fabs(r.cLon - lastLon) > 0.02 || now - lastC > 600.0)) {
                char u[400];
                std::snprintf(u, sizeof u, "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&%s", r.cLat, wrapLon(r.cLon), cur);
                lastC = now; lastLat = r.cLat; lastLon = r.cLon;
                if (curlGet(u, tmp_ + "wx_center.json", 10)) {
                    auto p = parse(readFile(tmp_ + "wx_center.json"));
                    std::lock_guard<std::mutex> lk(m_);
                    if (!p.empty()) { center_ = p[0]; status_ = "weather: Open-Meteo"; ++version_; }
                } else { std::lock_guard<std::mutex> lk(m_); status_ = "weather: Open-Meteo unavailable"; }
            }

            const double span = std::max(r.maxLat - r.minLat, r.maxLon - r.minLon);
            if (r.wantGrid && now - lastG > 30.0 && (std::fabs(r.cLat - lastGLat) + std::fabs(r.cLon - lastGLon) > span * 0.25 || now - lastG > 900.0)) {
                const int cols = 8, rows = 5;
                std::string la, lo;
                for (int j = 0; j < rows; ++j)
                    for (int i = 0; i < cols; ++i) {
                        const double lat = std::clamp(r.maxLat - (j + 0.5) * (r.maxLat - r.minLat) / rows, -89.0, 89.0);
                        const double lon = wrapLon(r.minLon + (i + 0.5) * (r.maxLon - r.minLon) / cols);
                        char b[40];
                        std::snprintf(b, sizeof b, "%.2f", lat); la += (la.empty() ? "" : ","); la += b;
                        std::snprintf(b, sizeof b, "%.2f", lon); lo += (lo.empty() ? "" : ","); lo += b;
                    }
                const std::string u = "https://api.open-meteo.com/v1/forecast?latitude=" + la + "&longitude=" + lo + "&current=wind_speed_10m,wind_direction_10m,wind_gusts_10m&wind_speed_unit=kn";
                lastG = now; lastGLat = r.cLat; lastGLon = r.cLon;
                if (curlGet(u, tmp_ + "wx_grid.json", 15)) {
                    auto p = parse(readFile(tmp_ + "wx_grid.json"));
                    std::lock_guard<std::mutex> lk(m_);
                    if (!p.empty()) { grid_ = p; ++version_; }
                }
            }
        }
    }
    static double wrapLon(double lon) { while (lon > 180.0) lon -= 360.0; while (lon < -180.0) lon += 360.0; return lon; }

    std::string tmp_, status_ = "weather: waiting...";
    std::mutex m_;
    std::thread th_;
    std::atomic<bool> on_{ false }, quit_{ false };
    std::atomic<unsigned> version_{ 0 };
    WeatherRequest req_;
    WeatherPoint center_;
    std::vector<WeatherPoint> grid_;
};

inline std::string gibsTimeSlot(double unixNow, int lagMinutes = 40, bool dashed = false) {
    const double slotStart = std::floor(unixNow / 600.0) * 600.0 - lagMinutes * 60.0;
    return formatTime(slotStart, dashed ? "%Y-%m-%dT%H-%M-00Z" : "%Y-%m-%dT%H:%M:00Z");
}
