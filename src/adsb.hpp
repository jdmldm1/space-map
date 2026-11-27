#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "net.hpp"

struct Aircraft {
    std::string hex, call, type, reg, squawk;
    double lat = 0, lon = 0, altFt = 0, gs = 0, track = 0, vrate = 0;
    bool ground = false, hasAlt = false;
    double stamp = 0;
    std::vector<std::pair<float, float>> trail;
};

struct AdsbRegion {
    bool valid = false;
    double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0, cLat = 0, cLon = 0, halfDiagNm = 0;
};

class AdsbFeed {
public:
    explicit AdsbFeed(std::string tmpFile) : tmp_(std::move(tmpFile)) {}
    ~AdsbFeed() { quit_ = true; if (th_.joinable()) th_.detach(); }

    void start() { th_ = std::thread([this] { loop(); }); }
    void setEnabled(bool on) { on_ = on; if (on) nextFetch_ = 0; }
    bool enabled() const { return on_; }
    void setRegion(const AdsbRegion& r) { std::lock_guard<std::mutex> lk(m_); region_ = r; }
    unsigned version() { return version_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }
    std::vector<Aircraft> snapshot() {
        std::lock_guard<std::mutex> lk(m_);
        std::vector<Aircraft> v;
        v.reserve(db_.size());
        for (auto& kv : db_) v.push_back(kv.second);
        return v;
    }

    static std::vector<Aircraft> parseAdsbLol(const std::string& s, double now) {
        std::vector<Aircraft> out;
        size_t p = s.find("\"ac\"");
        if (p == std::string::npos) return out;
        p = s.find('[', p);
        if (p == std::string::npos) return out;
        for (++p; p < s.size();) {
            while (p < s.size() && s[p] != '{' && s[p] != ']') ++p;
            if (p >= s.size() || s[p] == ']') break;
            size_t b = p, depth = 0;
            bool str = false;
            for (; p < s.size(); ++p) {
                const char c = s[p];
                if (str) { if (c == '\\') ++p; else if (c == '"') str = false; continue; }
                if (c == '"') str = true;
                else if (c == '{') ++depth;
                else if (c == '}' && --depth == 0) { ++p; break; }
            }
            const std::string o = s.substr(b, p - b);
            Aircraft a;
            if (!num(o, "lat", a.lat) || !num(o, "lon", a.lon)) continue;
            a.hex = str_(o, "hex"); a.call = trim(str_(o, "flight")); a.type = str_(o, "t"); a.reg = str_(o, "r"); a.squawk = str_(o, "squawk");
            const std::string ab = rawField(o, "alt_baro");
            if (ab == "\"ground\"") a.ground = true;
            else if (!ab.empty()) { a.altFt = std::atof(ab.c_str()); a.hasAlt = true; }
            num(o, "gs", a.gs); num(o, "track", a.track); num(o, "baro_rate", a.vrate);
            double seen = 0; num(o, "seen_pos", seen);
            a.stamp = now - seen;
            if (!a.hex.empty()) out.push_back(std::move(a));
        }
        return out;
    }

    static std::vector<Aircraft> parseOpenSky(const std::string& s, double now) {
        std::vector<Aircraft> out;
        size_t p = s.find("\"states\"");
        if (p == std::string::npos) return out;
        p = s.find('[', p);
        if (p == std::string::npos) return out;
        for (++p; p < s.size();) {
            while (p < s.size() && s[p] != '[' && s[p] != ']') ++p;
            if (p >= s.size() || s[p] == ']') break;
            ++p;
            std::vector<std::string> f;
            std::string cur;
            bool str = false;
            for (; p < s.size(); ++p) {
                const char c = s[p];
                if (str) { if (c == '"') str = false; else cur += c; continue; }
                if (c == '"') str = true;
                else if (c == ',') { f.push_back(cur); cur.clear(); }
                else if (c == ']') { f.push_back(cur); ++p; break; }
                else if (c == '[') {  }
                else cur += c;
            }
            if (f.size() < 15 || f[5] == "null" || f[6] == "null") continue;
            Aircraft a;
            a.hex = f[0]; a.call = trim(f[1]);
            a.lon = std::atof(f[5].c_str()); a.lat = std::atof(f[6].c_str());
            if (f[7] != "null" && !f[7].empty()) { a.altFt = std::atof(f[7].c_str()) * 3.28084; a.hasAlt = true; }
            a.ground = (f[8] == "true");
            if (f[9] != "null") a.gs = std::atof(f[9].c_str()) * 1.94384;
            if (f[10] != "null") a.track = std::atof(f[10].c_str());
            if (f[11] != "null") a.vrate = std::atof(f[11].c_str()) * 196.85;
            a.squawk = f[14] == "null" ? "" : f[14];
            double tpos = f[3] == "null" ? 0 : std::atof(f[3].c_str()), tnow = 0;
            num(s, "time", tnow);
            a.stamp = now - (tnow > 0 && tpos > 0 ? std::max(0.0, tnow - tpos) : 0.0);
            out.push_back(std::move(a));
        }
        return out;
    }

private:
    static std::string trim(std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
        size_t i = 0; while (i < s.size() && s[i] == ' ') ++i;
        return s.substr(i);
    }
    static std::string rawField(const std::string& o, const char* key) {
        const std::string pat = std::string("\"") + key + "\":";
        size_t p = o.find(pat);
        if (p == std::string::npos) return "";
        p += pat.size();
        while (p < o.size() && o[p] == ' ') ++p;
        size_t e = p;
        if (e < o.size() && o[e] == '"') { ++e; while (e < o.size() && o[e] != '"') ++e; ++e; }
        else while (e < o.size() && o[e] != ',' && o[e] != '}' && o[e] != ']') ++e;
        return o.substr(p, e - p);
    }
    static bool num(const std::string& o, const char* key, double& v) {
        const std::string r = rawField(o, key);
        if (r.empty() || r[0] == '"' || r == "null") return false;
        v = std::atof(r.c_str());
        return true;
    }
    static std::string str_(const std::string& o, const char* key) {
        std::string r = rawField(o, key);
        if (r.size() >= 2 && r.front() == '"') return r.substr(1, r.size() - 2);
        return r;
    }

    void loop() {
        int fails = 0;
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const double now = steadySeconds();
            if (!on_ || now < nextFetch_) continue;
            AdsbRegion r;
            { std::lock_guard<std::mutex> lk(m_); r = region_; }
            if (!r.valid) continue;

            std::string url, src;
            double interval;
            if (r.halfDiagNm <= 240.0) {
                char b[160];
                std::snprintf(b, sizeof b, "https://api.adsb.lol/v2/point/%.4f/%.4f/%d", r.cLat, wrap(r.cLon), (int)std::ceil(std::max(10.0, r.halfDiagNm)));
                url = b; src = "adsb.lol"; interval = 5.0;
            } else {
                const bool world = (r.maxLon - r.minLon) > 300.0 || (r.maxLat - r.minLat) > 120.0;
                char b[220];
                if (world) std::snprintf(b, sizeof b, "https://opensky-network.org/api/states/all");
                else std::snprintf(b, sizeof b, "https://opensky-network.org/api/states/all?lamin=%.3f&lamax=%.3f&lomin=%.3f&lomax=%.3f",
                                   std::max(-90.0, r.minLat), std::min(90.0, r.maxLat), std::max(-180.0, r.minLon), std::min(180.0, r.maxLon));
                url = b; src = "OpenSky"; interval = world ? 60.0 : 30.0;
            }
            const bool ok = curlGet(url, tmp_, 12);
            const double t = steadySeconds();
            if (!ok) {
                ++fails;
                const double wait = std::min(300.0, interval * (1 << std::min(fails, 5)));
                nextFetch_ = t + wait;
                std::lock_guard<std::mutex> lk(m_);
                status_ = "ADS-B: " + src + " unavailable (retry in " + std::to_string((int)wait) + " s)";
                continue;
            }
            fails = 0;
            nextFetch_ = t + interval;
            const std::string body = readFile(tmp_);
            std::vector<Aircraft> fresh = src == "adsb.lol" ? parseAdsbLol(body, t) : parseOpenSky(body, t);
            std::lock_guard<std::mutex> lk(m_);
            for (auto& a : fresh) {
                auto it = db_.find(a.hex);
                if (it == db_.end()) { a.trail.push_back({ (float)a.lat, (float)a.lon }); db_[a.hex] = std::move(a); continue; }
                Aircraft& o = it->second;
                std::vector<std::pair<float, float>> tr = std::move(o.trail);
                if (tr.empty() || std::fabs(tr.back().first - a.lat) + std::fabs(tr.back().second - a.lon) > 1e-4) tr.push_back({ (float)a.lat, (float)a.lon });
                if (tr.size() > 24) tr.erase(tr.begin());
                o = std::move(a);
                o.trail = std::move(tr);
            }
            for (auto it = db_.begin(); it != db_.end();) it = (t - it->second.stamp > 90.0) ? db_.erase(it) : std::next(it);
            status_ = "ADS-B: " + std::to_string(db_.size()) + " aircraft  (" + src + ")";
            ++version_;
        }
    }
    static double wrap(double lon) { while (lon > 180.0) lon -= 360.0; while (lon < -180.0) lon += 360.0; return lon; }

    std::string tmp_;
    std::mutex m_;
    std::thread th_;
    std::atomic<bool> on_{ false }, quit_{ false };
    std::atomic<unsigned> version_{ 0 };
    std::atomic<double> nextFetch_{ 0 };
    AdsbRegion region_;
    std::unordered_map<std::string, Aircraft> db_;
    std::string status_ = "ADS-B: waiting for data...";
};
