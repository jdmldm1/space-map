#pragma once
// Upcoming rocket launches from the Launch Library 2 public API (thespacedevs.com), polled every 20 minutes
// while the layer is on. The free tier is rate limited, so polling is deliberately gentle.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ais.hpp"
#include "hazards.hpp"
#include "net.hpp"

struct Launch {
    std::string name, provider, statusAbbrev, statusName, pad, location, mission, missionType;
    double net = 0, windowStart = 0, windowEnd = 0, lat = 0, lon = 0;
    bool hasPad = false;
};

// Substring of a nested JSON object (`"key": { ... }`) or empty when it is null/missing.
inline std::string jsonObject(const std::string& o, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = o.find(pat);
    while (p != std::string::npos) {
        size_t q = p + pat.size();
        while (q < o.size() && (o[q] == ' ' || o[q] == '\n' || o[q] == '\r' || o[q] == '\t')) ++q;
        if (q < o.size() && o[q] == ':') {
            ++q;
            while (q < o.size() && (o[q] == ' ' || o[q] == '\n' || o[q] == '\r' || o[q] == '\t')) ++q;
            if (q >= o.size() || o[q] != '{') return "";
            size_t depth = 0, e = q;
            bool str = false;
            for (; e < o.size(); ++e) {
                const char c = o[e];
                if (str) { if (c == '\\') ++e; else if (c == '"') str = false; continue; }
                if (c == '"') str = true;
                else if (c == '{') ++depth;
                else if (c == '}' && --depth == 0) { ++e; break; }
            }
            return o.substr(q, e - q);
        }
        p = o.find(pat, p + pat.size());
    }
    return "";
}

class LaunchFeed {
public:
    explicit LaunchFeed(std::string tmpFile) : tmp_(std::move(tmpFile)) {}
    ~LaunchFeed() { quit_ = true; if (th_.joinable()) th_.detach(); }
    void start() { th_ = std::thread([this] { loop(); }); }
    void setEnabled(bool on) { on_ = on; if (on && next_ > steadySeconds() + 60.0) next_ = 0; }
    bool enabled() const { return on_; }
    unsigned version() const { return version_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }
    std::vector<Launch> launches() { std::lock_guard<std::mutex> lk(m_); return list_; }

    static std::vector<Launch> parse(const std::string& s) {
        std::vector<Launch> out;
        for (const std::string& o : ais::objectsOf(s, s.find("\"results\""))) {
            Launch l;
            l.name = ais::jraw(o, "name");
            if (l.name.empty()) continue;
            const std::string status = jsonObject(o, "status");
            l.statusAbbrev = ais::jraw(status, "abbrev"); l.statusName = ais::jraw(status, "name");
            l.net = parseIsoTime(ais::jraw(o, "net"));
            l.windowStart = parseIsoTime(ais::jraw(o, "window_start")); l.windowEnd = parseIsoTime(ais::jraw(o, "window_end"));
            l.provider = ais::jraw(jsonObject(o, "launch_service_provider"), "name");
            const std::string mission = jsonObject(o, "mission");
            l.mission = ais::jraw(mission, "name"); l.missionType = ais::jraw(mission, "type");
            const std::string pad = jsonObject(o, "pad");
            l.pad = ais::jraw(pad, "name");
            l.location = ais::jraw(jsonObject(pad, "location"), "name");
            double la = 0, lo = 0;
            l.hasPad = ais::jnum(pad, "latitude", la) && ais::jnum(pad, "longitude", lo);
            l.lat = la; l.lon = lo;
            if (l.net > 0) out.push_back(std::move(l));
        }
        std::sort(out.begin(), out.end(), [](const Launch& a, const Launch& b) { return a.net < b.net; });
        return out;
    }

private:
    void loop() {
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            const double now = steadySeconds();
            if (!on_ || now < next_) continue;
            next_ = now + 1200.0;
            if (curlGet("https://ll.thespacedevs.com/2.2.0/launch/upcoming/?limit=20&hide_recent_previous=true", tmp_, 25, true)) {
                auto l = parse(readFile(tmp_));
                std::lock_guard<std::mutex> lk(m_);
                if (!l.empty()) { list_ = std::move(l); status_ = "launches: " + std::to_string(list_.size()) + " upcoming (Launch Library 2)"; ++version_; }
                else status_ = "launches: no data returned (rate limited?)";
            } else {
                next_ = now + 180.0;
                std::lock_guard<std::mutex> lk(m_);
                status_ = "launches: Launch Library unavailable";
            }
        }
    }
    std::string tmp_, status_;
    std::mutex m_;
    std::thread th_;
    std::atomic<bool> quit_{ false }, on_{ false };
    std::atomic<unsigned> version_{ 0 };
    double next_ = 0;
    std::vector<Launch> list_;
};
