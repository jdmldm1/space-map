#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "net.hpp"

struct Vessel {
    int mmsi = 0, imo = 0, shipType = 0, navStat = 15, heading = 511, len = 0, beam = 0;
    std::string name, callsign, dest, eta, src;
    double lat = 0, lon = 0, sog = 0, cog = 0, draught = 0;
    double stamp = 0;
    bool hasPos = false;
    std::vector<std::pair<float, float>> trail;
};

namespace ais {

// AIS ship type 35 is military operations; 55 is law enforcement. Both are shown by the military filter.
inline bool isMilitaryVessel(const Vessel& v) { return v.shipType == 35 || v.shipType == 55; }

inline const char* shipTypeName(int t) {
    if (t >= 20 && t < 30) return "Wing in ground";
    if (t == 30) return "Fishing";
    if (t == 31 || t == 32) return "Towing";
    if (t == 33) return "Dredging / underwater ops";
    if (t == 34) return "Diving ops";
    if (t == 35) return "Military ops";
    if (t == 36) return "Sailing";
    if (t == 37) return "Pleasure craft";
    if (t >= 40 && t < 50) return "High-speed craft";
    if (t == 50) return "Pilot vessel";
    if (t == 51) return "Search & rescue";
    if (t == 52) return "Tug";
    if (t == 53) return "Port tender";
    if (t == 54) return "Anti-pollution";
    if (t == 55) return "Law enforcement";
    if (t == 58) return "Medical transport";
    if (t == 59) return "Special craft";
    if (t >= 60 && t < 70) return "Passenger";
    if (t >= 70 && t < 80) return "Cargo";
    if (t >= 80 && t < 90) return "Tanker";
    if (t >= 90 && t < 100) return "Other";
    return "Unknown";
}

inline int category(int t) {
    if (t >= 70 && t < 80) return 0;
    if (t >= 80 && t < 90) return 1;
    if ((t >= 60 && t < 70) || (t >= 40 && t < 50)) return 2;
    if (t == 30) return 3;
    return 4;
}
inline const char* navStatusName(int s) {
    static const char* n[] = { "Under way (engine)", "At anchor", "Not under command", "Restricted manoeuvrability", "Constrained by draught", "Moored", "Aground",
                               "Fishing", "Under way (sailing)", "Reserved", "Reserved", "Towing astern", "Pushing / towing alongside", "Reserved", "AIS-SART active", "Not defined" };
    return (s >= 0 && s < 16) ? n[s] : "Not defined";
}

inline const char* mmsiCountry(int mmsi) {
    static const std::map<int, const char*> mid = {
        { 201, "Albania" }, { 202, "Andorra" }, { 203, "Austria" }, { 205, "Belgium" }, { 206, "Belarus" }, { 207, "Bulgaria" }, { 209, "Cyprus" }, { 210, "Cyprus" },
        { 211, "Germany" }, { 212, "Cyprus" }, { 213, "Georgia" }, { 214, "Moldova" }, { 215, "Malta" }, { 218, "Germany" }, { 219, "Denmark" }, { 220, "Denmark" },
        { 224, "Spain" }, { 225, "Spain" }, { 226, "France" }, { 227, "France" }, { 228, "France" }, { 229, "Malta" }, { 230, "Finland" }, { 231, "Faroe Islands" },
        { 232, "United Kingdom" }, { 233, "United Kingdom" }, { 234, "United Kingdom" }, { 235, "United Kingdom" }, { 236, "Gibraltar" }, { 237, "Greece" }, { 238, "Croatia" },
        { 239, "Greece" }, { 240, "Greece" }, { 241, "Greece" }, { 242, "Morocco" }, { 243, "Hungary" }, { 244, "Netherlands" }, { 245, "Netherlands" }, { 246, "Netherlands" },
        { 247, "Italy" }, { 248, "Malta" }, { 249, "Malta" }, { 250, "Ireland" }, { 251, "Iceland" }, { 253, "Luxembourg" }, { 254, "Monaco" }, { 255, "Portugal" },
        { 256, "Malta" }, { 257, "Norway" }, { 258, "Norway" }, { 259, "Norway" }, { 261, "Poland" }, { 262, "Montenegro" }, { 263, "Portugal" }, { 264, "Romania" },
        { 265, "Sweden" }, { 266, "Sweden" }, { 267, "Slovakia" }, { 269, "Switzerland" }, { 270, "Czechia" }, { 271, "Turkey" }, { 272, "Ukraine" }, { 273, "Russia" },
        { 274, "North Macedonia" }, { 275, "Latvia" }, { 276, "Estonia" }, { 277, "Lithuania" }, { 278, "Slovenia" }, { 279, "Serbia" },
        { 303, "United States (Alaska)" }, { 304, "Antigua & Barbuda" }, { 305, "Antigua & Barbuda" }, { 308, "Bahamas" }, { 309, "Bahamas" }, { 310, "Bermuda" },
        { 311, "Bahamas" }, { 312, "Belize" }, { 314, "Barbados" }, { 316, "Canada" }, { 319, "Cayman Islands" }, { 321, "Costa Rica" }, { 323, "Cuba" }, { 329, "Guadeloupe" },
        { 338, "United States" }, { 339, "Jamaica" }, { 345, "Mexico" }, { 351, "Panama" }, { 352, "Panama" }, { 353, "Panama" }, { 354, "Panama" }, { 355, "Panama" },
        { 356, "Panama" }, { 357, "Panama" }, { 366, "United States" }, { 367, "United States" }, { 368, "United States" }, { 369, "United States" }, { 370, "Panama" },
        { 371, "Panama" }, { 372, "Panama" }, { 373, "Panama" }, { 374, "Panama" }, { 377, "St Vincent & Grenadines" }, { 378, "British Virgin Islands" },
        { 403, "Saudi Arabia" }, { 405, "Bangladesh" }, { 408, "Bahrain" }, { 412, "China" }, { 413, "China" }, { 414, "China" }, { 416, "Taiwan" }, { 417, "Sri Lanka" },
        { 419, "India" }, { 422, "Iran" }, { 423, "Azerbaijan" }, { 425, "Iraq" }, { 428, "Israel" }, { 431, "Japan" }, { 432, "Japan" }, { 436, "Kazakhstan" },
        { 440, "South Korea" }, { 441, "South Korea" }, { 445, "North Korea" }, { 447, "Kuwait" }, { 450, "Lebanon" }, { 461, "Oman" }, { 463, "Pakistan" }, { 466, "Qatar" },
        { 468, "Syria" }, { 470, "United Arab Emirates" }, { 471, "United Arab Emirates" }, { 473, "Yemen" }, { 477, "Hong Kong" },
        { 503, "Australia" }, { 506, "Myanmar" }, { 508, "Brunei" }, { 512, "New Zealand" }, { 520, "Fiji" }, { 525, "Indonesia" }, { 533, "Malaysia" }, { 538, "Marshall Islands" },
        { 548, "Philippines" }, { 553, "Papua New Guinea" }, { 563, "Singapore" }, { 564, "Singapore" }, { 565, "Singapore" }, { 566, "Singapore" }, { 567, "Thailand" },
        { 574, "Vietnam" }, { 601, "South Africa" }, { 603, "Angola" }, { 605, "Algeria" }, { 612, "Cameroon" }, { 620, "Comoros" }, { 622, "Egypt" }, { 625, "Eritrea" },
        { 627, "Gabon" }, { 629, "Gambia" }, { 636, "Liberia" }, { 637, "Liberia" }, { 642, "Libya" }, { 649, "Mali" }, { 654, "Mauritania" }, { 656, "Niger" },
        { 657, "Nigeria" }, { 659, "Namibia" }, { 663, "Senegal" }, { 664, "Seychelles" }, { 667, "Sierra Leone" }, { 672, "Tunisia" }, { 674, "Tanzania" },
        { 701, "Argentina" }, { 710, "Brazil" }, { 725, "Chile" }, { 730, "Colombia" }, { 735, "Ecuador" }, { 745, "Guyana" }, { 750, "Guyana" }, { 760, "Peru" }, { 770, "Uruguay" },
        { 775, "Venezuela" },
    };
    auto it = mid.find(mmsi / 1000000);
    return it == mid.end() ? "" : it->second;
}

struct Msg {
    int type = 0, mmsi = 0, navStat = 15, heading = 511, shipType = 0, imo = 0, len = 0, beam = 0, partNo = -1;
    bool hasPos = false, hasStatic = false;
    double lat = 0, lon = 0, sog = 0, cog = 0, draught = 0;
    std::string name, callsign, dest, eta;
};

class Bits {
public:
    explicit Bits(const std::string& payload) {
        for (char ch : payload) {
            int v = (unsigned char)ch - 48;
            if (v > 40) v -= 8;
            for (int i = 5; i >= 0; --i) b_.push_back((v >> i) & 1);
        }
    }
    size_t size() const { return b_.size(); }
    uint32_t u(size_t start, size_t len) const {
        uint32_t v = 0;
        for (size_t i = 0; i < len; ++i) v = (v << 1) | (start + i < b_.size() ? b_[start + i] : 0);
        return v;
    }
    int32_t s(size_t start, size_t len) const {
        uint32_t v = u(start, len);
        if (len < 32 && (v & (1u << (len - 1)))) v |= ~((1u << len) - 1u);
        return (int32_t)v;
    }
    std::string str(size_t start, size_t chars) const {
        std::string out;
        for (size_t i = 0; i < chars; ++i) {
            const uint32_t v = u(start + i * 6, 6);
            out += (char)(v < 32 ? v + 64 : v);
        }
        while (!out.empty() && (out.back() == '@' || out.back() == ' ')) out.pop_back();
        return out;
    }
private:
    std::vector<unsigned char> b_;
};

inline bool decode(const std::string& payload, Msg& m) {
    Bits b(payload);
    if (b.size() < 38) return false;
    m.type = (int)b.u(0, 6);
    m.mmsi = (int)b.u(8, 30);
    auto pos = [&](size_t lonAt, size_t latAt, size_t sogAt, size_t cogAt, size_t hdgAt) {
        const int32_t lo = b.s(lonAt, 28), la = b.s(latAt, 27);
        if (lo == 108600000 || la == 54600000) return;
        m.lon = lo / 600000.0; m.lat = la / 600000.0; m.hasPos = true;
        const uint32_t sg = b.u(sogAt, 10), cg = b.u(cogAt, 12);
        m.sog = sg >= 1023 ? 0.0 : sg / 10.0;
        m.cog = cg >= 3600 ? 0.0 : cg / 10.0;
        m.heading = (int)b.u(hdgAt, 9);
    };
    switch (m.type) {
        case 1: case 2: case 3:
            if (b.size() < 137) return false;
            m.navStat = (int)b.u(38, 4);
            pos(61, 89, 50, 116, 128);
            return m.hasPos;
        case 18:
            if (b.size() < 133) return false;
            pos(57, 85, 46, 112, 124);
            return m.hasPos;
        case 19:
            if (b.size() < 272) return false;
            pos(57, 85, 46, 112, 124);
            m.name = b.str(143, 20); m.shipType = (int)b.u(263, 8); m.hasStatic = true;
            return m.hasPos;
        case 5: {
            if (b.size() < 420) return false;
            m.imo = (int)b.u(40, 30); m.callsign = b.str(70, 7); m.name = b.str(112, 20); m.shipType = (int)b.u(232, 8);
            m.len = (int)(b.u(240, 9) + b.u(249, 9)); m.beam = (int)(b.u(258, 6) + b.u(264, 6));
            const int mo = (int)b.u(274, 4), dy = (int)b.u(278, 5), hr = (int)b.u(283, 5), mi = (int)b.u(288, 6);
            if (mo >= 1 && mo <= 12 && dy >= 1 && hr < 24 && mi < 60) { char t[24]; std::snprintf(t, sizeof t, "%02d-%02d %02d:%02d", mo, dy, hr, mi); m.eta = t; }
            m.draught = b.u(294, 8) / 10.0; m.dest = b.str(302, 20); m.hasStatic = true;
            return true;
        }
        case 24: {
            m.partNo = (int)b.u(38, 2);
            if (m.partNo == 0) { if (b.size() < 160) return false; m.name = b.str(40, 20); }
            else { if (b.size() < 162) return false; m.shipType = (int)b.u(40, 8); m.callsign = b.str(90, 7);
                   m.len = (int)(b.u(132, 9) + b.u(141, 9)); m.beam = (int)(b.u(150, 6) + b.u(156, 6)); }
            m.hasStatic = true;
            return true;
        }
        default: return false;
    }
}

class NmeaParser {
public:

    bool feed(const std::string& rawLine, Msg& m) {
        size_t p = rawLine.find("!AIVDM");
        if (p == std::string::npos) p = rawLine.find("!BSVDM");
        if (p == std::string::npos) return false;
        std::string line = rawLine.substr(p);
        std::vector<std::string> f;
        size_t b = 0;
        for (size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == ',' || line[i] == '*') { f.push_back(line.substr(b, i - b)); b = i + 1; if (i < line.size() && line[i] == '*') break; }
        if (f.size() < 6) return false;
        const int count = std::atoi(f[1].c_str()), num = std::atoi(f[2].c_str());
        const std::string key = f[3] + f[4];
        if (count <= 1) return decode(f[5], m);
        if (num == 1) parts_[key] = f[5]; else if (parts_.count(key)) parts_[key] += f[5];
        if (num == count && parts_.count(key)) { const std::string full = parts_[key]; parts_.erase(key); return decode(full, m); }
        if (parts_.size() > 64) parts_.clear();
        return false;
    }
private:
    std::map<std::string, std::string> parts_;
};

inline std::string jraw(const std::string& o, const char* key, size_t from = 0) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = from;
    while ((p = o.find(pat, p)) != std::string::npos) {
        size_t q = p + pat.size();
        while (q < o.size() && (o[q] == ' ' || o[q] == '\n' || o[q] == '\r' || o[q] == '\t')) ++q;
        if (q < o.size() && o[q] == ':') {
            ++q;
            while (q < o.size() && (o[q] == ' ' || o[q] == '\n' || o[q] == '\r' || o[q] == '\t')) ++q;
            size_t e = q;
            if (e < o.size() && o[e] == '"') { ++e; while (e < o.size() && o[e] != '"') { if (o[e] == '\\') ++e; ++e; } return o.substr(q + 1, e - q - 1); }
            while (e < o.size() && o[e] != ',' && o[e] != '}' && o[e] != ']' && o[e] != '\n' && o[e] != '\r') ++e;
            std::string v = o.substr(q, e - q);
            while (!v.empty() && v.back() == ' ') v.pop_back();
            return v;
        }
        p += pat.size();
    }
    return "";
}
inline bool jnum(const std::string& o, const char* key, double& v) {
    const std::string r = jraw(o, key);
    if (r.empty() || r == "null") return false;
    v = std::atof(r.c_str());
    return true;
}
inline std::string trimStr(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0; while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

inline std::vector<std::string> objectsOf(const std::string& s, size_t from) {
    std::vector<std::string> out;
    size_t p = s.find('[', from);
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
        out.push_back(s.substr(b, p - b));
    }
    return out;
}

inline std::vector<Vessel> parseDigitrafficLocations(const std::string& s, double steadyNow, double unixNow) {
    std::vector<Vessel> out;
    for (const std::string& f : objectsOf(s, s.find("\"features\""))) {
        Vessel v;
        double d;
        if (!jnum(f, "mmsi", d)) continue;
        v.mmsi = (int)d;
        const size_t c = f.find("\"coordinates\"");
        if (c == std::string::npos) continue;
        const size_t lb = f.find('[', c);
        if (lb == std::string::npos) continue;
        v.lon = std::atof(f.c_str() + lb + 1);
        const size_t cm = f.find(',', lb);
        if (cm == std::string::npos) continue;
        v.lat = std::atof(f.c_str() + cm + 1);
        if (std::fabs(v.lat) > 90.0 || std::fabs(v.lon) > 180.0) continue;
        if (jnum(f, "sog", d)) v.sog = d >= 102.3 ? 0.0 : d;
        if (jnum(f, "cog", d)) v.cog = d >= 360.0 ? 0.0 : d;
        if (jnum(f, "heading", d)) v.heading = (int)d;
        if (jnum(f, "navStat", d)) v.navStat = (int)d;
        double ts = 0;
        const double age = jnum(f, "timestampExternal", ts) ? std::max(0.0, unixNow - ts / 1000.0) : 0.0;
        if (age > 3600.0) continue;
        v.stamp = steadyNow - age;
        v.hasPos = true;
        v.src = "Digitraffic";
        out.push_back(std::move(v));
    }
    return out;
}

inline std::vector<Vessel> parseDigitrafficVessels(const std::string& s) {
    std::vector<Vessel> out;
    for (const std::string& o : objectsOf(s, 0)) {
        Vessel v;
        double d;
        if (!jnum(o, "mmsi", d)) continue;
        v.mmsi = (int)d;
        v.name = trimStr(jraw(o, "name")); v.callsign = trimStr(jraw(o, "callSign")); v.dest = trimStr(jraw(o, "destination"));
        if (jnum(o, "imo", d)) v.imo = (int)d;
        if (jnum(o, "shipType", d)) v.shipType = (int)d;
        if (jnum(o, "draught", d)) v.draught = d / 10.0;
        double a = 0, b = 0, c = 0, dd = 0;
        jnum(o, "referencePointA", a); jnum(o, "referencePointB", b); jnum(o, "referencePointC", c); jnum(o, "referencePointD", dd);
        v.len = (int)(a + b); v.beam = (int)(c + dd);
        if (jnum(o, "eta", d) && d > 0) {
            const int e = (int)d, mo = (e >> 16) & 15, dy = (e >> 11) & 31, hr = (e >> 6) & 31, mi = e & 63;
            if (mo >= 1 && mo <= 12 && dy >= 1 && hr < 24 && mi < 60) { char t[24]; std::snprintf(t, sizeof t, "%02d-%02d %02d:%02d", mo, dy, hr, mi); v.eta = t; }
        }
        out.push_back(std::move(v));
    }
    return out;
}

}

class AisFeed {
public:
    explicit AisFeed(std::string tmpDir, std::string nmeaHostPort) : tmp_(std::move(tmpDir)), nmea_(std::move(nmeaHostPort)) {}
    ~AisFeed() { quit_ = true; if (th_.joinable()) th_.detach(); }

    void start() { th_ = std::thread([this] { loop(); }); }
    void setEnabled(bool on) { on_ = on; if (on) { nextPos_ = 0; nextNmea_ = 0; } }
    bool enabled() const { return on_; }
    unsigned version() const { return version_; }
    std::string status() { std::lock_guard<std::mutex> lk(m_); return status_; }
    bool hasNmea() const { return !nmea_.empty(); }
    std::vector<Vessel> snapshot() {
        std::lock_guard<std::mutex> lk(m_);
        std::vector<Vessel> v;
        v.reserve(db_.size());
        for (auto& kv : db_) if (kv.second.hasPos) v.push_back(kv.second);
        return v;
    }

    void apply(const ais::Msg& m, double now, const char* src) {
        Vessel& v = db_[m.mmsi];
        v.mmsi = m.mmsi;
        if (m.hasPos) {
            if (v.hasPos && (std::fabs(v.lat - m.lat) + std::fabs(v.lon - m.lon) > 1e-4)) pushTrail(v);
            else if (!v.hasPos) v.trail.clear();
            v.lat = m.lat; v.lon = m.lon; v.sog = m.sog; v.cog = m.cog; v.heading = m.heading; if (m.type <= 3) v.navStat = m.navStat;
            v.stamp = now; v.hasPos = true; v.src = src;
        }
        if (m.hasStatic) {
            if (!m.name.empty()) v.name = m.name;
            if (!m.callsign.empty()) v.callsign = m.callsign;
            if (!m.dest.empty()) v.dest = m.dest;
            if (!m.eta.empty()) v.eta = m.eta;
            if (m.imo) v.imo = m.imo;
            if (m.shipType) v.shipType = m.shipType;
            if (m.len) v.len = m.len;
            if (m.beam) v.beam = m.beam;
            if (m.draught > 0) v.draught = m.draught;
        }
    }

private:
    static void pushTrail(Vessel& v) {
        v.trail.push_back({ (float)v.lat, (float)v.lon });
        if (v.trail.size() > 30) v.trail.erase(v.trail.begin());
    }

    void loop() {
        int failsPos = 0;
        ais::NmeaParser parser;
        std::string srcNote;
        while (!quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (!on_) continue;
            const double now = steadySeconds();
            bool changed = false;

            if (now >= nextMeta_ && now >= nextPos_) {
                const std::string f = tmp_ + "ais_vessels.json";
                if (curlGet("https://meri.digitraffic.fi/api/ais/v1/vessels", f, 40, true)) {
                    const auto info = ais::parseDigitrafficVessels(readFile(f));
                    std::lock_guard<std::mutex> lk(m_);
                    for (const Vessel& s : info) { Vessel& v = db_[s.mmsi]; v.mmsi = s.mmsi; v.name = s.name; v.callsign = s.callsign; v.dest = s.dest; v.eta = s.eta; v.imo = s.imo;
                                                   v.shipType = s.shipType; v.draught = s.draught; v.len = s.len; v.beam = s.beam; }
                    nextMeta_ = now + 900.0;
                    changed = true;
                } else nextMeta_ = now + 60.0;
            }
            if (now >= nextPos_) {
                const std::string f = tmp_ + "ais_locations.json";
                if (curlGet("https://meri.digitraffic.fi/api/ais/v1/locations", f, 25, true)) {
                    failsPos = 0;
                    const double unixNow = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
                    auto fresh = ais::parseDigitrafficLocations(readFile(f), steadySeconds(), unixNow);
                    std::lock_guard<std::mutex> lk(m_);
                    for (Vessel& p : fresh) {
                        Vessel& v = db_[p.mmsi];
                        if (v.hasPos && v.src == "Digitraffic" && (std::fabs(v.lat - p.lat) + std::fabs(v.lon - p.lon) > 1e-4)) pushTrail(v);
                        else if (!v.hasPos) v.trail.clear();
                        v.mmsi = p.mmsi; v.lat = p.lat; v.lon = p.lon; v.sog = p.sog; v.cog = p.cog; v.heading = p.heading; v.navStat = p.navStat;
                        v.stamp = p.stamp; v.hasPos = true; v.src = "Digitraffic";
                    }
                    nextPos_ = now + 30.0;
                    changed = true;
                } else {
                    ++failsPos;
                    nextPos_ = now + std::min(300.0, 30.0 * (1 << std::min(failsPos, 4)));
                    std::lock_guard<std::mutex> lk(m_);
                    status_ = "AIS: Digitraffic unavailable (retrying)";
                }
            }
            if (!nmea_.empty() && now >= nextNmea_) {
                const std::string f = tmp_ + "ais_nmea.txt";
                curlCapture("telnet://" + nmea_, f, 8);
                const std::string body = readFile(f);
                size_t b = 0, n = 0;
                std::lock_guard<std::mutex> lk(m_);
                const double t = steadySeconds();
                for (size_t i = 0; i <= body.size(); ++i)
                    if (i == body.size() || body[i] == '\n') {
                        ais::Msg msg;
                        if (parser.feed(body.substr(b, i - b), msg)) { apply(msg, t, "NMEA"); ++n; }
                        b = i + 1;
                    }
                nextNmea_ = steadySeconds() + 12.0;
                changed = changed || n > 0;
            }
            if (changed) {
                std::lock_guard<std::mutex> lk(m_);
                const double t = steadySeconds();
                for (auto it = db_.begin(); it != db_.end();) it = (it->second.hasPos && t - it->second.stamp > 5400.0) ? db_.erase(it) : std::next(it);
                size_t withPos = 0;
                for (auto& kv : db_) withPos += kv.second.hasPos;
                status_ = "AIS: " + std::to_string(withPos) + " vessels  (Digitraffic" + (nmea_.empty() ? ", Baltic / Finnish waters)" : " + NMEA " + nmea_ + ")");
                ++version_;
            }
        }
    }

    std::string tmp_, nmea_;
    std::mutex m_;
    std::thread th_;
    std::atomic<bool> on_{ false }, quit_{ false };
    std::atomic<unsigned> version_{ 0 };
    std::atomic<double> nextPos_{ 0 }, nextNmea_{ 0 }, nextMeta_{ 0 };
    std::unordered_map<int, Vessel> db_;
    std::string status_ = "AIS: waiting for data...";
};
