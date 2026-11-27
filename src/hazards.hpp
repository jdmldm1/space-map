#pragma once
// Live hazard and space-weather feeds, polled on a background thread while their layer is switched on:
//   * earthquakes          USGS magnitude 2.5+ in the past day
//   * natural events       NASA EONET open events (wildfires, volcanoes, severe storms, floods ...)
//   * space weather        NOAA SWPC planetary Kp index and GOES X-ray flux
//   * aurora forecast      NOAA SWPC OVATION model grid
// All sources are public and need no key.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ais.hpp"
#include "net.hpp"
#include "time_utils.hpp"

struct Quake {
    double lat = 0, lon = 0, depthKm = 0, mag = 0, time = 0;
    bool tsunami = false;
    std::string place;
};

struct NaturalEvent {
    double lat = 0, lon = 0, time = 0, magnitude = 0;
    std::string title, category, unit;
};

struct AuroraPoint { float lat, lon; unsigned char p; };

struct SpaceWeather {
    bool kpOk = false, xrayOk = false, auroraOk = false;
    double kp = 0, xrayFlux = 0, auroraForecast = 0;
};

inline double parseIsoTime(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 3) return 0.0;
    return utcToUnix(y, mo, d, h, mi, sec);
}

inline std::string xrayClass(double flux) {
    if (flux <= 0) return "?";
    const char* letters = "ABCMX";
    const double bases[] = { 1e-8, 1e-7, 1e-6, 1e-5, 1e-4 };
    int k = 0;
    for (int i = 0; i < 5; ++i) if (flux >= bases[i]) k = i;
    char b[16];
    std::snprintf(b, sizeof b, "%c%.1f", letters[k], flux / bases[k]);
    return b;
}

// NOAA geomagnetic storm scale from Kp.
inline const char* geomagneticStorm(double kp) {
    if (kp >= 9.0) return "G5 extreme storm";
    if (kp >= 8.0) return "G4 severe storm";
    if (kp >= 7.0) return "G3 strong storm";
    if (kp >= 6.0) return "G2 moderate storm";
    if (kp >= 5.0) return "G1 minor storm";
    if (kp >= 4.0) return "active";
    return "quiet";
}

class HazardFeed {
public:
    explicit HazardFeed(std::string directory) : dir_(std::move(directory)) {}
    ~HazardFeed() { quit_ = true; if (th_.joinable()) th_.detach(); }

    void start() { th_ = std::thread([this] { loop(); }); }
    void setQuakes(bool on) { quakesOn_ = on; if (on) nextQuake_ = 0; }
    void setEvents(bool on) { eventsOn_ = on; if (on) nextEvent_ = 0; }
    void setAurora(bool on) { auroraOn_ = on; if (on) { nextAurora_ = 0; nextWx_ = 0; } }
    unsigned version() const { return version_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }
    std::vector<Quake> quakes() { std::lock_guard<std::mutex> lk(m_); return quakes_; }
    std::vector<NaturalEvent> events() { std::lock_guard<std::mutex> lk(m_); return events_; }
    std::vector<AuroraPoint> aurora() { std::lock_guard<std::mutex> lk(m_); return aurora_; }
    SpaceWeather weather() { std::lock_guard<std::mutex> lk(m_); return wx_; }

    // ---- parsers (static so they can be tested without the network) -----------------------------------------------
    static std::vector<Quake> parseQuakes(const std::string& s) {
        std::vector<Quake> out;
        for (const std::string& f : ais::objectsOf(s, s.find("\"features\""))) {
            Quake q;
            if (!ais::jnum(f, "mag", q.mag)) continue;
            double t = 0, ts = 0;
            if (ais::jnum(f, "time", t)) q.time = t / 1000.0;
            if (ais::jnum(f, "tsunami", ts)) q.tsunami = ts > 0.5;
            q.place = ais::jraw(f, "place");
            const size_t c = f.find("\"coordinates\"");
            if (c == std::string::npos) continue;
            const char* p = f.c_str() + f.find('[', c) + 1;
            char* end = nullptr;
            q.lon = std::strtod(p, &end); if (end == p) continue; p = end; if (*p == ',') ++p;
            q.lat = std::strtod(p, &end); if (end == p) continue; p = end; if (*p == ',') ++p;
            q.depthKm = std::strtod(p, &end);
            out.push_back(std::move(q));
        }
        return out;
    }

    static std::vector<NaturalEvent> parseEvents(const std::string& s) {
        std::vector<NaturalEvent> out;
        for (const std::string& e : ais::objectsOf(s, s.find("\"events\""))) {
            NaturalEvent ev;
            ev.title = ais::jraw(e, "title");
            const size_t cat = e.find("\"categories\"");
            if (cat != std::string::npos) ev.category = ais::jraw(e, "id", cat);
            const size_t geom = e.find("\"geometry\"");
            const size_t c = e.rfind("\"coordinates\"");
            if (c == std::string::npos || geom == std::string::npos || c < geom) continue;
            size_t b = e.find('[', c) + 1;
            while (b < e.size() && (e[b] == ' ' || e[b] == '\n' || e[b] == '\t' || e[b] == '\r')) ++b;
            if (b >= e.size() || e[b] == '[') continue;  // polygons (e.g. sea ice) are skipped
            char* end = nullptr;
            const char* p = e.c_str() + b;
            ev.lon = std::strtod(p, &end); if (end == p) continue; p = end; if (*p == ',') ++p;
            ev.lat = std::strtod(p, &end); if (end == p) continue;
            const size_t d = e.rfind("\"date\"");
            if (d != std::string::npos) ev.time = parseIsoTime(ais::jraw(e, "date", d));
            const size_t mv = e.rfind("\"magnitudeValue\"");
            if (mv != std::string::npos && mv > geom) { ev.magnitude = std::atof(ais::jraw(e, "magnitudeValue", mv).c_str()); ev.unit = ais::jraw(e, "magnitudeUnit", mv); }
            out.push_back(std::move(ev));
        }
        return out;
    }

    static std::vector<AuroraPoint> parseAurora(const std::string& s, double& forecastTime) {
        std::vector<AuroraPoint> out;
        forecastTime = parseIsoTime(ais::jraw(s, "Forecast Time"));
        size_t c = s.find("\"coordinates\"");
        if (c == std::string::npos) return out;
        const char* base = s.c_str();
        const char* p = base + s.find('[', c) + 1;
        const char* endAll = base + s.size();
        while (p < endAll) {
            while (p < endAll && (*p == ' ' || *p == ',' || *p == '\n')) ++p;
            if (p >= endAll || *p == ']') break;
            if (*p != '[') { ++p; continue; }
            ++p;
            char* end = nullptr;
            const double lon = std::strtod(p, &end); p = end; if (*p == ',') ++p;
            const double lat = std::strtod(p, &end); p = end; if (*p == ',') ++p;
            const double val = std::strtod(p, &end); p = end;
            while (p < endAll && *p != ']') ++p;
            if (p < endAll) ++p;
            if (val >= 3.0 && out.size() < 40000) out.push_back({ (float)lat, (float)(lon > 180.0 ? lon - 360.0 : lon), (unsigned char)std::min(100.0, val) });
        }
        return out;
    }

private:
    void loop() {
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            const double now = steadySeconds();
            if (quakesOn_ && now >= nextQuake_) {
                nextQuake_ = now + 300.0;
                if (curlGet("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson", dir_ + "quakes_tmp.json", 20, true)) {
                    auto q = parseQuakes(readFile(dir_ + "quakes_tmp.json"));
                    std::lock_guard<std::mutex> lk(m_);
                    quakes_ = std::move(q);
                    ++version_;
                } else nextQuake_ = now + 60.0;
            }
            if (eventsOn_ && now >= nextEvent_) {
                nextEvent_ = now + 900.0;
                if (curlGet("https://eonet.gsfc.nasa.gov/api/v3/events?status=open&limit=500", dir_ + "events_tmp.json", 30, true)) {
                    auto e = parseEvents(readFile(dir_ + "events_tmp.json"));
                    std::lock_guard<std::mutex> lk(m_);
                    events_ = std::move(e);
                    ++version_;
                } else nextEvent_ = now + 90.0;
            }
            if (auroraOn_ && now >= nextWx_) {
                nextWx_ = now + 300.0;
                SpaceWeather w;
                if (curlGet("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json", dir_ + "kp_tmp.json", 20, true)) {
                    const auto objs = ais::objectsOf(readFile(dir_ + "kp_tmp.json"), 0);
                    if (!objs.empty()) w.kpOk = ais::jnum(objs.back(), "Kp", w.kp);
                }
                if (curlGet("https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json", dir_ + "xray_tmp.json", 25, true)) {
                    const auto objs = ais::objectsOf(readFile(dir_ + "xray_tmp.json"), 0);
                    for (size_t i = objs.size(); i-- > 0;) if (ais::jraw(objs[i], "energy") == "0.1-0.8nm") { w.xrayOk = ais::jnum(objs[i], "flux", w.xrayFlux); break; }
                }
                std::lock_guard<std::mutex> lk(m_);
                w.auroraOk = wx_.auroraOk; w.auroraForecast = wx_.auroraForecast;
                wx_ = w;
                ++version_;
            }
            if (auroraOn_ && now >= nextAurora_) {
                nextAurora_ = now + 600.0;
                if (curlGet("https://services.swpc.noaa.gov/json/ovation_aurora_latest.json", dir_ + "aurora_tmp.json", 40, true)) {
                    double ft = 0;
                    auto a = parseAurora(readFile(dir_ + "aurora_tmp.json"), ft);
                    std::lock_guard<std::mutex> lk(m_);
                    aurora_ = std::move(a);
                    wx_.auroraOk = true; wx_.auroraForecast = ft;
                    ++version_;
                } else nextAurora_ = now + 90.0;
            }
            std::lock_guard<std::mutex> lk(m_);
            std::string st;
            if (quakesOn_) st += "earthquakes: " + std::to_string(quakes_.size()) + " (USGS)";
            if (eventsOn_) st += std::string(st.empty() ? "" : "   ·   ") + "natural events: " + std::to_string(events_.size()) + " (NASA EONET)";
            if (auroraOn_) st += std::string(st.empty() ? "" : "   ·   ") + (wx_.auroraOk ? "aurora forecast (NOAA OVATION)" : "aurora: loading");
            status_ = st;
        }
    }

    std::string dir_, status_;
    std::mutex m_;
    std::thread th_;
    std::atomic<bool> quit_{ false }, quakesOn_{ false }, eventsOn_{ false }, auroraOn_{ false };
    std::atomic<unsigned> version_{ 0 };
    double nextQuake_ = 0, nextEvent_ = 0, nextAurora_ = 0, nextWx_ = 0;
    std::vector<Quake> quakes_;
    std::vector<NaturalEvent> events_;
    std::vector<AuroraPoint> aurora_;
    SpaceWeather wx_;
};
