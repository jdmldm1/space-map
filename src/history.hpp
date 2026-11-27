#pragma once
// Rolling position history for aircraft and ships, so the map can show where traffic was and replay it.
// Samples are thinned (one per aircraft every ~25 s), kept for a few hours and saved to disk between runs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct HistSample {
    double t = 0;
    float lat = 0, lon = 0, alt = -1, heading = 0;
    bool ground = false;
};

struct HistTrack {
    std::string label, type;
    int shipType = 0;
    std::vector<HistSample> s;
};

class TrackHistory {
public:
    std::unordered_map<std::string, HistTrack> tracks;

    void add(const std::string& id, const std::string& label, const std::string& type, int shipType, const HistSample& sample, double minGap) {
        HistTrack& t = tracks[id];
        if (!t.s.empty() && sample.t - t.s.back().t < minGap) return;
        if (!t.s.empty() && sample.t < t.s.back().t) return;
        if (!label.empty()) t.label = label;
        if (!type.empty()) t.type = type;
        t.shipType = shipType;
        t.s.push_back(sample);
    }

    void prune(double now, double keepSeconds) {
        for (auto it = tracks.begin(); it != tracks.end();) {
            auto& v = it->second.s;
            v.erase(v.begin(), std::lower_bound(v.begin(), v.end(), now - keepSeconds, [](const HistSample& a, double t) { return a.t < t; }));
            if (v.empty()) it = tracks.erase(it); else ++it;
        }
    }

    size_t sampleCount() const { size_t n = 0; for (const auto& kv : tracks) n += kv.second.s.size(); return n; }
    double oldest() const { double o = 1e18; for (const auto& kv : tracks) if (!kv.second.s.empty()) o = std::min(o, kv.second.s.front().t); return o; }

    // Position of a track at time t, interpolated between samples. Returns false when the track was not reporting then
    // (before its first sample, or after a gap longer than maxGap). `trail` receives the positions of the previous trailSeconds.
    static bool stateAt(const HistTrack& tr, double t, double maxGap, HistSample& out, std::vector<std::pair<float, float>>* trail, double trailSeconds) {
        const auto& s = tr.s;
        if (s.empty() || t < s.front().t) return false;
        const size_t i = (size_t)(std::upper_bound(s.begin(), s.end(), t, [](double tt, const HistSample& a) { return tt < a.t; }) - s.begin());
        const HistSample& prev = s[i - 1];
        if (t - prev.t > maxGap && (i == s.size() || s[i].t - prev.t > maxGap * 2.0)) return false;
        out = prev;
        if (i < s.size() && s[i].t - prev.t <= maxGap * 2.0 && s[i].t > prev.t) {
            const HistSample& nx = s[i];
            const double f = (t - prev.t) / (nx.t - prev.t);
            float dl = nx.lon - prev.lon;
            if (dl > 180.0f) dl -= 360.0f; else if (dl < -180.0f) dl += 360.0f;
            out.lat = prev.lat + (nx.lat - prev.lat) * (float)f;
            out.lon = prev.lon + dl * (float)f;
            if (prev.alt >= 0 && nx.alt >= 0) out.alt = prev.alt + (nx.alt - prev.alt) * (float)f;
        }
        out.t = t;
        if (trail) {
            trail->clear();
            for (size_t k = i; k-- > 0 && s[k].t >= t - trailSeconds;) trail->push_back({ s[k].lat, s[k].lon });
            std::reverse(trail->begin(), trail->end());
        }
        return true;
    }

    void save(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        if (!f) return;
        auto wstr = [&](const std::string& v) { const uint16_t n = (uint16_t)std::min<size_t>(v.size(), 60000); f.write((const char*)&n, 2); f.write(v.data(), n); };
        f.write("SMH2", 4);
        const uint32_t n = (uint32_t)tracks.size();
        f.write((const char*)&n, 4);
        for (const auto& kv : tracks) {
            wstr(kv.first); wstr(kv.second.label); wstr(kv.second.type);
            const int32_t st = kv.second.shipType; f.write((const char*)&st, 4);
            const uint32_t m = (uint32_t)kv.second.s.size(); f.write((const char*)&m, 4);
            for (const HistSample& s : kv.second.s) {
                const uint8_t g = s.ground ? 1 : 0;
                f.write((const char*)&s.t, 8); f.write((const char*)&s.lat, 4); f.write((const char*)&s.lon, 4); f.write((const char*)&s.alt, 4); f.write((const char*)&s.heading, 4); f.write((const char*)&g, 1);
            }
        }
    }

    void load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return;
        char magic[4] = {};
        f.read(magic, 4);
        if (std::string(magic, 4) != "SMH2") return;
        auto rstr = [&](std::string& v) { uint16_t n = 0; f.read((char*)&n, 2); v.resize(n); if (n) f.read(&v[0], n); };
        uint32_t n = 0;
        f.read((char*)&n, 4);
        for (uint32_t i = 0; i < n && f; ++i) {
            std::string id; HistTrack tr;
            rstr(id); rstr(tr.label); rstr(tr.type);
            int32_t st = 0; uint32_t m = 0;
            f.read((char*)&st, 4); f.read((char*)&m, 4);
            if (!f || m > 100000) return;
            tr.shipType = st;
            tr.s.resize(m);
            for (HistSample& s : tr.s) {
                uint8_t g = 0;
                f.read((char*)&s.t, 8); f.read((char*)&s.lat, 4); f.read((char*)&s.lon, 4); f.read((char*)&s.alt, 4); f.read((char*)&s.heading, 4); f.read((char*)&g, 1);
                s.ground = g != 0;
            }
            if (f) tracks[id] = std::move(tr);
        }
    }
};
