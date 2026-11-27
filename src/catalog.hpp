#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "meta.hpp"
#include "orbit.hpp"
#include "paths.hpp"
#include "raylib.h"

struct Body {
    std::string name;
    double radiusKm;
    double mu;
    double spinRate;
    Vec3d pos;
    Color glow;
};

enum class Kind { Satellite, Station, Ship, Constellation };

struct Entity {
    std::string name;
    Kind kind;
    Orbit orbit;
    int parent = 0;
    Color color = WHITE;
    bool labeled = false;
    bool dot = false;
    int norad = 0;
    Vector3 pos{};
    double altKm = 0;

    std::string intl, group;
    int launchYear = 0, launchNo = 0, rev = 0, oclass = 0, cat = 5;
    double bstar = 0, revPerDay = 0;
};

inline constexpr float kSceneUnitsPerKm = 0.001f;

inline Vector3 toRender(const Vec3d& p) { return { (float)(p.x * kSceneUnitsPerKm), (float)(p.z * kSceneUnitsPerKm), (float)(-p.y * kSceneUnitsPerKm) }; }

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::Satellite: return "Satellite";
        case Kind::Station: return "Space station";
        case Kind::Ship: return "Spacecraft";
        default: return "Constellation sat";
    }
}

inline void addEntity(std::vector<Entity>& v, const char* name, Kind kind, Orbit o, Color c, bool labeled) {
    Entity e;
    e.name = name; e.kind = kind; e.orbit = o; e.color = c; e.labeled = labeled;
    e.dot = (kind == Kind::Constellation) || (kind == Kind::Satellite && !labeled);
    v.push_back(std::move(e));
}

inline void buildScene(std::vector<Entity>& s, const Body& earth) {
    const double R = earth.radiusKm;

    addEntity(s, "ISS", Kind::Station, Orbit::circular(R + 420, 51.64, 40, 0), { 255, 255, 255, 255 }, true);
    addEntity(s, "Tiangong", Kind::Station, Orbit::circular(R + 390, 41.47, 200, 120), { 255, 90, 90, 255 }, true);
    addEntity(s, "Crew Dragon", Kind::Ship, Orbit::circular(R + 418, 51.64, 40, -0.4), { 255, 170, 40, 255 }, true);
    addEntity(s, "Cargo Tug", Kind::Ship, Orbit::circular(R + 800, 28.5, 300, 200), { 255, 170, 40, 255 }, true);

    {
        double rp = 7000, ra = 90000;
        addEntity(s, "Deep Space Freighter", Kind::Ship,
                  Orbit::make((rp + ra) / 2, (ra - rp) / (ra + rp), 23.4, 80, 45, 10), { 255, 120, 255, 255 }, true);
    }

    addEntity(s, "Hubble", Kind::Satellite, Orbit::circular(R + 540, 28.47, 250, 90), { 120, 200, 255, 255 }, true);
    addEntity(s, "Sun-Sync EO", Kind::Satellite, Orbit::circular(R + 700, 98.2, 10, 60), { 120, 255, 160, 255 }, true);

    addEntity(s, "Molniya", Kind::Satellite, Orbit::make(26562, 0.74, 63.4, 100, 270, 0), { 255, 240, 120, 255 }, true);

    for (int i = 0; i < 3; ++i) {
        char n[16]; std::snprintf(n, sizeof n, "GEO-%d", i + 1);
        addEntity(s, n, Kind::Satellite, Orbit::circular(42164, 0.05, 0, 120.0 * i), { 200, 160, 255, 255 }, true);
    }

    for (int p = 0; p < 6; ++p)
        for (int k = 0; k < 4; ++k) {
            char n[24]; std::snprintf(n, sizeof n, "NAV-%d%d", p + 1, k + 1);
            addEntity(s, n, Kind::Satellite, Orbit::circular(26560, 55, 60.0 * p, 90.0 * k + 15.0 * p), { 90, 255, 120, 255 }, false);
        }

    for (int p = 0; p < 22; ++p)
        for (int k = 0; k < 20; ++k) {
            char n[32]; std::snprintf(n, sizeof n, "LEO-A-%d-%d", p + 1, k + 1);
            addEntity(s, n, Kind::Constellation, Orbit::circular(R + 550, 53.0, p * 360.0 / 22, k * 18.0 + p * 8.2), { 80, 200, 255, 255 }, false);
        }
    for (int p = 0; p < 6; ++p)
        for (int k = 0; k < 10; ++k) {
            char n[32]; std::snprintf(n, sizeof n, "LEO-B-%d-%d", p + 1, k + 1);
            addEntity(s, n, Kind::Constellation, Orbit::circular(R + 1200, 87.9, p * 30.0, k * 36.0 + p * 6.0), { 255, 150, 60, 255 }, false);
        }
}

inline double tleEpochUnix(int yy, double doy) {
    int year = yy < 57 ? 2000 + yy : 1900 + yy;
    auto leaps = [](long long y) { return y / 4 - y / 100 + y / 400; };
    long long days = 365LL * (year - 1970) + leaps(year - 1) - leaps(1969);
    return ((double)days + (doy - 1.0)) * 86400.0;
}

inline bool has(const std::string& s, const char* k) { return s.find(k) != std::string::npos; }

inline constexpr int kSatCategories = 6;
inline const char* satCategoryName(int c) {
    static const char* names[kSatCategories] = { "Starlink", "Other broadband (OneWeb, Iridium ...)", "Navigation (GPS, Galileo ...)", "Weather and Earth observation", "Stations and crew vehicles", "Everything else" };
    return names[c < 0 || c >= kSatCategories ? kSatCategories - 1 : c];
}

// Groups satellites for the filter toggles, using the group names produced by satGroup().
inline int satCategory(const Entity& e) {
    const std::string& g = e.group;
    if (e.kind == Kind::Station || e.kind == Kind::Ship) return 4;
    if (has(g, "Starlink")) return 0;
    if (has(g, "OneWeb") || has(g, "Iridium") || has(g, "Globalstar") || has(g, "Orbcomm") || has(g, "Kuiper")) return 1;
    if (has(g, "GPS") || has(g, "Galileo") || has(g, "BeiDou") || has(g, "GLONASS")) return 2;
    if (has(g, "weather") || has(g, "MetOp") || has(g, "Meteosat") || has(g, "Himawari") || has(g, "Landsat") || has(g, "Sentinel") || has(g, "Earth Observing")) return 3;
    return 5;
}

inline void classify(Entity& e, bool fromStations) {
    const std::string& n = e.name;
    const double apo = e.orbit.a * (1.0 + e.orbit.e);
    if (has(n, "DRAGON") || has(n, "SOYUZ") || has(n, "PROGRESS") || has(n, "CYGNUS") || has(n, "SHENZHOU") ||
        has(n, "TIANZHOU") || has(n, "HTV") || has(n, "STARLINER")) {
        e.kind = Kind::Ship; e.color = { 236, 240, 246, 255 }; e.labeled = false;
    } else if (fromStations) {
        e.kind = Kind::Station; e.color = has(n, "ZARYA") ? Color{ 255, 255, 255, 255 } : Color{ 225, 230, 238, 255 };
        e.labeled = has(n, "ZARYA") || has(n, "TIANHE");
    } else if (has(n, "STARLINK") || has(n, "ONEWEB") || has(n, "IRIDIUM") || has(n, "GLOBALSTAR") ||
               has(n, "ORBCOMM") || has(n, "KUIPER")) {
        e.kind = Kind::Constellation; e.color = has(n, "ONEWEB") ? Color{ 170, 178, 190, 255 } : Color{ 150, 165, 185, 255 };
    } else {
        e.kind = Kind::Satellite;
        if (e.orbit.e > 0.25) e.color = { 236, 240, 246, 255 };
        else if (apo > 30000) e.color = { 150, 160, 176, 255 };
        else if (apo > 3000) e.color = { 188, 198, 212, 255 };
        else e.color = { 205, 214, 226, 255 };
        e.labeled = (n == "HST");
        if (e.labeled) e.color = { 240, 244, 250, 255 };
    }
    e.dot = (e.kind == Kind::Constellation) || (e.kind == Kind::Satellite && !e.labeled);
}

inline bool matchesSatCategory(const Entity& e, int cat) {
    if (cat <= 0) return true;
    const std::string& n = e.name;
    switch (cat) {
        case 1: // Stations & Crewed
            return e.kind == Kind::Station || e.kind == Kind::Ship ||
                   has(n, "ISS") || has(n, "TIANGONG") || has(n, "ZARYA") || has(n, "TIANHE") ||
                   has(n, "DRAGON") || has(n, "SOYUZ") || has(n, "SHENZHOU") || has(n, "CYGNUS");
        case 2: // Starlink
            return has(n, "STARLINK");
        case 3: // Navigation (GNSS)
            return has(n, "NAVSTAR") || has(n, "GPS ") || has(n, "GALILEO") ||
                   has(n, "BEIDOU") || has(n, "GLONASS") || has(n, "COMPASS") || has(n, "QZSS");
        case 4: // Weather & Earth Observation
            return has(n, "GOES") || has(n, "NOAA") || has(n, "METOP") || has(n, "METEOSAT") ||
                   has(n, "HIMAWARI") || has(n, "LANDSAT") || has(n, "SENTINEL") || has(n, "TERRA") ||
                   has(n, "AQUA") || n == "HST";
        case 5: // Comms
            return has(n, "ONEWEB") || has(n, "IRIDIUM") || has(n, "GLOBALSTAR") ||
                   has(n, "ORBCOMM") || has(n, "KUIPER");
        case 6: { // Geostationary
            const double apo = e.orbit.a * (1.0 + e.orbit.e);
            return e.oclass == 2 || (apo > 35000.0 && e.orbit.e < 0.1);
        }
        default:
            return true;
    }
}

inline double parseBstar(const std::string& l1) {
    const std::string f = l1.substr(53, 8);
    const double mant = std::atoi(f.substr(1, 5).c_str()) * 1e-5;
    const int ex = (f[6] == '-' ? -1 : 1) * (f[7] - '0');
    return (f[0] == '-' ? -1.0 : 1.0) * mant * std::pow(10.0, ex);
}

inline int loadTleFile(const std::string& path, std::unordered_set<int>& seen, std::vector<Entity>& out, bool fromStations) {
    std::ifstream f(path);
    if (!f) return 0;
    std::string n, l1, l2;
    int count = 0;
    while (std::getline(f, n) && std::getline(f, l1) && std::getline(f, l2)) {
        while (!n.empty() && (n.back() == ' ' || n.back() == '\r')) n.pop_back();
        if (l1.size() < 63 || l2.size() < 63 || l1[0] != '1' || l2[0] != '2') continue;
        int id = std::atoi(l1.substr(2, 5).c_str());
        if (!seen.insert(id).second) continue;
        auto num = [](const std::string& str, size_t pos, size_t len) { return std::strtod(str.substr(pos, len).c_str(), nullptr); };
        double rev = num(l2, 52, 11);
        if (rev <= 0.0) continue;
        double epoch = tleEpochUnix((int)num(l1, 18, 2), num(l1, 20, 12));
        double ecc = std::strtod(("0." + l2.substr(26, 7)).c_str(), nullptr);
        Entity e;
        e.name = n; e.norad = id;
        e.orbit = Orbit::fromTle(num(l2, 8, 8), num(l2, 17, 8), ecc, num(l2, 34, 8), num(l2, 43, 8), rev, epoch);
        e.orbit.enableSgp4(parseBstar(l1));
        e.bstar = parseBstar(l1); e.revPerDay = rev;
        e.rev = std::atoi(l2.substr(63, 5).c_str());
        {
            const int yy = std::atoi(l1.substr(9, 2).c_str());
            e.launchYear = yy < 57 ? 2000 + yy : 1900 + yy;
            e.launchNo = std::atoi(l1.substr(11, 3).c_str());
            std::string piece = l1.substr(14, 3);
            while (!piece.empty() && piece.back() == ' ') piece.pop_back();
            char b[32]; std::snprintf(b, sizeof b, "%04d-%03d%s", e.launchYear, e.launchNo, piece.c_str());
            e.intl = b;
            e.group = satGroup(n);
            const double perigee = e.orbit.a * (1.0 - ecc) - 6378.0, apogee = e.orbit.a * (1.0 + ecc) - 6378.0, periodMin = 1440.0 / rev;
            e.oclass = ecc > 0.25 ? 3 : (periodMin > 1380 && periodMin < 1500) ? 2 : apogee < 2000 ? 0 : 1;
            (void)perigee;
        }
        classify(e, fromStations);
        e.cat = satCategory(e);
        out.push_back(std::move(e));
        ++count;
    }
    return count;
}

inline std::vector<Entity> loadSatelliteCatalog(const Body& earth) {
    std::vector<Entity> entities;
    entities.reserve(20000);
    std::unordered_set<int> seenNoradIds;
    struct TleFile { const char* name; bool isStationGroup; };
    for (const TleFile& file : { TleFile{ "stations.txt", true }, TleFile{ "active.txt", false } }) {
        const std::string refreshedPath = paths::dataPath("tle/") + file.name;
        const std::string bundledPath = paths::assetPath(std::string("assets/tle/") + file.name);
        int loaded = loadTleFile(refreshedPath, seenNoradIds, entities, file.isStationGroup);
        std::string source = refreshedPath;
        if (loaded == 0) {
            loaded = loadTleFile(bundledPath, seenNoradIds, entities, file.isStationGroup);
            source = bundledPath;
        }
        TraceLog(LOG_INFO, "Loaded %d TLEs from %s", loaded, source.c_str());
    }
    if (entities.empty()) {
        TraceLog(LOG_WARNING, "No TLE data found, using built-in demo scene");
        buildScene(entities, earth);
    }
    size_t sgp4Count = 0;
    for (const Entity& entity : entities) sgp4Count += entity.orbit.useSgp4;
    TraceLog(LOG_INFO, "Propagators: %zu SGP4 (near-Earth), %zu Kepler+J2 (deep space)", sgp4Count, entities.size() - sgp4Count);
    return entities;
}
