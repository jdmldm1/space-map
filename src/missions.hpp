#pragma once
// Spacecraft mission database: past, active and planned missions with real trajectories.
//
// Trajectories come from NASA/JPL Horizons (see scripts/gen_missions.py) and are stored in assets/missions/*.csv.
// Samples carry position and velocity, so positions between them are interpolated with cubic Hermite splines.
// Starship is the one exception: Horizons has no trajectory for it, so a representative suborbital flight profile
// is generated here and is labelled as such in the app.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "orbit.hpp"
#include "paths.hpp"
#include "raylib.h"
#include "time_utils.hpp"

enum class MFrame { Sun, EarthInertial, EarthFixed };
enum class MStatus { Past, Active, Planned };

struct MEvent {
    double t = 0;
    std::string label;
    bool approx = false;  // only the month is known (planned events)
};

struct Mission {
    std::string key, name, agency, kind, target, summary, note;
    MFrame frame = MFrame::Sun;
    Color color{ 255, 255, 255, 255 };
    double launch = 0;
    double end = 0;  // 0 while the mission is still operating
    bool plannedOverride = false;  // launch not yet confirmed: the path is a predicted trajectory
    bool launchKnown = true;  // false when only the start of the trajectory data is known
    std::vector<MEvent> events;
    std::vector<double> t;                  // sample times, unix seconds
    std::vector<std::array<double, 6>> s;   // x y z (AU or km) and velocity per second

    bool loaded() const { return t.size() > 1; }
    double tStart() const { return t.empty() ? 0.0 : t.front(); }
    double tEnd() const { return t.empty() ? 0.0 : t.back(); }
    bool covers(double when) const { return loaded() && when >= t.front() && when <= t.back(); }

    // Position in the mission's frame (AU heliocentric, or km geocentric). Clamped to the trajectory ends.
    Vec3d at(double when) const {
        if (t.empty()) return { 0, 0, 0 };
        if (when <= t.front()) return { s.front()[0], s.front()[1], s.front()[2] };
        if (when >= t.back()) return { s.back()[0], s.back()[1], s.back()[2] };
        const size_t i = (size_t)(std::upper_bound(t.begin(), t.end(), when) - t.begin());
        const size_t a = i - 1, b = i;
        const double h = t[b] - t[a], u = (when - t[a]) / h, u2 = u * u, u3 = u2 * u;
        const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        Vec3d p;
        double* out[3] = { &p.x, &p.y, &p.z };
        for (int k = 0; k < 3; ++k) *out[k] = h00 * s[a][k] + h10 * h * s[a][3 + k] + h01 * s[b][k] + h11 * h * s[b][3 + k];
        return p;
    }
    // Speed in the frame's native unit per second (AU/s or km/s), by central difference.
    double speedAt(double when) const {
        const double d = 30.0;
        const Vec3d a = at(when - d), b = at(when + d);
        return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z)) / (2.0 * d);
    }
    MStatus status(double now) const {
        if (plannedOverride || launch > now) return MStatus::Planned;
        if (end != 0 && end < now) return MStatus::Past;
        return MStatus::Active;
    }
};

// High-precision outer-planet positions (heliocentric AU, equatorial J2000) so spacecraft line up with the planet they visit.
struct PlanetTracks {
    struct Track { std::vector<double> t; std::vector<std::array<double, 6>> s; };
    std::array<Track, 9> planet;

    bool position(int idx, double when, Vec3d& out) const {
        if (idx < 0 || idx >= (int)planet.size()) return false;
        const Track& tr = planet[idx];
        if (tr.t.size() < 2 || when < tr.t.front() || when > tr.t.back()) return false;
        const size_t i = (size_t)(std::upper_bound(tr.t.begin(), tr.t.end(), when) - tr.t.begin());
        const size_t a = i - 1, b = std::min(i, tr.t.size() - 1);
        const double h = tr.t[b] - tr.t[a], u = (when - tr.t[a]) / h, u2 = u * u, u3 = u2 * u;
        const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        double* o[3] = { &out.x, &out.y, &out.z };
        for (int k = 0; k < 3; ++k) *o[k] = h00 * tr.s[a][k] + h10 * h * tr.s[a][3 + k] + h01 * tr.s[b][k] + h11 * h * tr.s[b][3 + k];
        return true;
    }
};

inline PlanetTracks loadPlanetTracks() {
    PlanetTracks out;
    std::ifstream f(paths::assetPath("assets/missions/planets.csv"));
    std::string line;
    size_t n = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        double v[8];
        const char* p = line.c_str();
        char* end = nullptr;
        bool ok = true;
        for (int k = 0; k < 8; ++k) {
            v[k] = std::strtod(p, &end);
            if (end == p) { ok = false; break; }
            p = end;
            if (*p == ',') ++p;
        }
        const int idx = (int)v[0];
        if (!ok || idx < 0 || idx >= (int)out.planet.size()) continue;
        out.planet[idx].t.push_back(v[1]);
        out.planet[idx].s.push_back({ v[2], v[3], v[4], v[5] / 86400.0, v[6] / 86400.0, v[7] / 86400.0 });
        ++n;
    }
    TraceLog(LOG_INFO, "Missions: %zu outer-planet samples", n);
    return out;
}

inline const char* missionStatusName(MStatus s) { return s == MStatus::Past ? "Completed" : s == MStatus::Active ? "Active" : "Planned"; }
inline Color missionStatusColor(MStatus s) { return s == MStatus::Past ? Color{ 160, 168, 185, 255 } : s == MStatus::Active ? Color{ 90, 255, 140, 255 } : Color{ 255, 205, 90, 255 }; }

namespace missiondb {

inline double day(int y, int m, int d, int hh = 0, int mm = 0) { return utcToUnix(y, m, d, hh, mm, 0); }

struct Meta {
    const char* key; const char* name; const char* agency; const char* kind; const char* target; const char* summary;
    MFrame frame; Color color; double launch, end;
};

struct Ev { const char* key; double t; const char* label; bool approx; };

// Colours are hand-picked to stay distinguishable against the dark background and each other.
inline const std::vector<Meta>& table() {
    static const std::vector<Meta> t = {
        { "voyager1", "Voyager 1", "NASA", "Outer planets flyby, interstellar probe", "Interstellar space",
          "Flew past Jupiter and Saturn, then became the first human-made object to enter interstellar space. It is the most distant spacecraft from Earth.", MFrame::Sun, { 255, 214, 90, 255 }, day(1977, 9, 5, 12, 56), 0 },
        { "voyager2", "Voyager 2", "NASA", "Grand Tour probe", "Interstellar space",
          "The only spacecraft to have visited Uranus and Neptune. Used a rare planetary alignment to hop from Jupiter to Saturn, Uranus and Neptune, and later crossed into interstellar space.", MFrame::Sun, { 255, 140, 70, 255 }, day(1977, 8, 20, 14, 29), 0 },
        { "pioneer10", "Pioneer 10", "NASA", "Jupiter flyby", "Interstellar space",
          "First spacecraft to cross the asteroid belt and the first to fly past Jupiter. Contact was lost in 2003 and it is coasting out of the Solar System.", MFrame::Sun, { 170, 200, 255, 255 }, day(1972, 3, 3, 1, 49), day(2003, 1, 23) },
        { "pioneer11", "Pioneer 11", "NASA", "Jupiter and Saturn flyby", "Interstellar space",
          "Flew past Jupiter and made the first ever flyby of Saturn. Operations ended in 1995.", MFrame::Sun, { 140, 150, 255, 255 }, day(1973, 4, 6, 2, 11), day(1995, 9, 30) },
        { "galileo", "Galileo", "NASA", "Jupiter orbiter and atmospheric probe", "Jupiter",
          "First spacecraft to orbit Jupiter and to drop a probe into its atmosphere. Visited two asteroids (Gaspra, Ida) on the way and was deliberately sent into Jupiter to protect Europa.", MFrame::Sun, { 255, 160, 120, 255 }, day(1989, 10, 18, 16, 53), day(2003, 9, 21) },
        { "cassini", "Cassini-Huygens", "NASA / ESA / ASI", "Saturn orbiter and Titan lander", "Saturn",
          "Orbited Saturn for 13 years and landed the Huygens probe on Titan. Ended with the Grand Finale, diving between Saturn and its rings before burning up in the atmosphere.", MFrame::Sun, { 240, 215, 150, 255 }, day(1997, 10, 15, 8, 43), day(2017, 9, 15) },
        { "stardust", "Stardust", "NASA", "Comet sample return", "Comet Wild 2",
          "Flew through the coma of comet Wild 2, collected dust and returned it to Earth in a capsule in 2006, then went on to visit comet Tempel 1.", MFrame::Sun, { 200, 120, 255, 255 }, day(1999, 2, 7, 21, 4), day(2011, 3, 24) },
        { "hayabusa", "Hayabusa", "JAXA", "Asteroid sample return", "Asteroid Itokawa",
          "First spacecraft to return a sample from an asteroid. Reached Itokawa in 2005; the path shown is the final return leg to Earth (trajectory data starts in 2009).", MFrame::Sun, { 255, 150, 190, 255 }, day(2003, 5, 9, 4, 29), day(2010, 6, 13) },
        { "rosetta", "Rosetta / Philae", "ESA", "Comet orbiter and lander", "Comet 67P",
          "First spacecraft to orbit a comet and to land on one. Used four gravity assists to reach 67P/Churyumov-Gerasimenko, and ended with a controlled landing on the comet.", MFrame::Sun, { 110, 235, 175, 255 }, day(2004, 3, 2, 7, 17), day(2016, 9, 30) },
        { "messenger", "MESSENGER", "NASA", "Mercury orbiter", "Mercury",
          "First spacecraft to orbit Mercury. Six planetary flybys slowed it enough for orbit insertion in 2011; it mapped the entire planet before impacting the surface.", MFrame::Sun, { 190, 170, 160, 255 }, day(2004, 8, 3, 6, 15), day(2015, 4, 30) },
        { "dawn", "Dawn", "NASA", "Asteroid belt orbiter", "Vesta and Ceres",
          "First spacecraft to orbit two bodies beyond Earth: the asteroid Vesta and the dwarf planet Ceres, using ion propulsion.", MFrame::Sun, { 120, 220, 255, 255 }, day(2007, 9, 27, 11, 34), day(2018, 11, 1) },
        { "newhorizons", "New Horizons", "NASA", "Pluto and Kuiper belt flyby", "Kuiper belt",
          "Made the first flyby of Pluto in 2015 and then explored the Kuiper belt object Arrokoth. Still operating and heading out of the Solar System.", MFrame::Sun, { 255, 120, 200, 255 }, day(2006, 1, 19, 19, 0), 0 },
        { "msl", "Curiosity (cruise)", "NASA", "Mars rover", "Mars",
          "The Mars Science Laboratory cruise from Earth to Mars, ending with the Curiosity rover's landing in Gale crater. Path shown is the interplanetary cruise.", MFrame::Sun, { 255, 100, 90, 255 }, day(2011, 11, 26, 15, 2), day(2012, 8, 6, 5, 17) },
        { "mars2020", "Perseverance (cruise)", "NASA", "Mars rover", "Mars",
          "Mars 2020 cruise from Earth to Mars, ending with the Perseverance rover landing in Jezero crater. Path shown is the interplanetary cruise.", MFrame::Sun, { 255, 175, 60, 255 }, day(2020, 7, 30, 11, 50), day(2021, 2, 18, 20, 55) },
        { "dart", "DART", "NASA", "Planetary defence test", "Asteroid Dimorphos",
          "First test of deflecting an asteroid: the spacecraft deliberately struck Dimorphos and measurably shortened its orbit around Didymos.", MFrame::Sun, { 255, 235, 110, 255 }, day(2021, 11, 24, 6, 21), day(2022, 9, 26, 23, 14) },
        { "hayabusa2", "Hayabusa2", "JAXA", "Asteroid sample return, extended mission", "Asteroid 1998 KY26",
          "Returned samples from asteroid Ryugu in 2020. Its extended mission flies past the asteroid Torifune (2001 CC21) in 2026 and rendezvous with the tiny, fast-spinning 1998 KY26 in 2031.", MFrame::Sun, { 255, 110, 160, 255 }, day(2014, 12, 3, 4, 22), 0 },
        { "osirisrex", "OSIRIS-REx / APEX", "NASA", "Asteroid sample return, extended mission", "Asteroid Apophis",
          "Collected a sample from asteroid Bennu and returned it to Earth in 2023. Renamed OSIRIS-APEX, it is now heading for the asteroid Apophis, which passes close to Earth in 2029.", MFrame::Sun, { 130, 190, 255, 255 }, day(2016, 9, 9, 0, 5), 0 },
        { "juno", "Juno", "NASA", "Jupiter polar orbiter", "Jupiter",
          "Studies Jupiter's interior, magnetic field and auroras from a highly elliptical polar orbit that dips beneath the radiation belts.", MFrame::Sun, { 255, 190, 120, 255 }, day(2011, 8, 5, 16, 25), 0 },
        { "parker", "Parker Solar Probe", "NASA", "Solar probe", "The Sun",
          "Flies through the Sun's corona, using repeated Venus flybys to tighten its orbit. It is the fastest human-made object and the closest to the Sun.", MFrame::Sun, { 255, 230, 100, 255 }, day(2018, 8, 12, 7, 31), 0 },
        { "lucy", "Lucy", "NASA", "Jupiter Trojan asteroid tour", "Jupiter Trojans",
          "The first mission to visit the Jupiter Trojan asteroids, two swarms that share Jupiter's orbit. A twelve-year tour with three Earth gravity assists.", MFrame::Sun, { 175, 240, 120, 255 }, day(2021, 10, 16, 9, 34), 0 },
        { "psyche", "Psyche", "NASA", "Metal asteroid orbiter", "Asteroid (16) Psyche",
          "Heading to the metal-rich asteroid 16 Psyche, possibly the exposed core of an early planet, using solar-electric propulsion and a Mars gravity assist.", MFrame::Sun, { 200, 170, 255, 255 }, day(2023, 10, 13, 14, 19), 0 },
        { "bepicolombo", "BepiColombo", "ESA / JAXA", "Mercury orbiters", "Mercury",
          "Two orbiters travelling together to Mercury, slowed by flybys of Earth, Venus and six passes of Mercury itself before orbit insertion.", MFrame::Sun, { 150, 230, 210, 255 }, day(2018, 10, 20, 1, 45), 0 },
        { "ulysses", "Ulysses", "ESA / NASA", "Solar polar orbiter", "The Sun's poles",
          "The first spacecraft to fly over the Sun's poles. A Jupiter gravity assist swung it out of the plane of the planets into a polar orbit around the Sun, and it completed three polar passes.", MFrame::Sun, { 255, 190, 60, 255 }, day(1990, 10, 6, 11, 47), day(2009, 6, 30) },
        { "solarorbiter", "Solar Orbiter", "ESA / NASA", "Sun observatory", "The Sun",
          "Observes the Sun from inside Mercury's orbit. Repeated Venus flybys tilt its orbit so it can image the Sun's poles for the first time.", MFrame::Sun, { 255, 170, 90, 255 }, day(2020, 2, 10, 4, 3), 0 },
        { "hera", "Hera", "ESA", "Asteroid deflection follow-up", "Didymos and Dimorphos",
          "Follows up NASA's DART impact: it will survey the Didymos double asteroid and measure how the impact changed Dimorphos, including its crater and mass.", MFrame::Sun, { 160, 255, 200, 255 }, day(2024, 10, 7, 14, 52), 0 },
        { "escapade", "ESCAPADE", "NASA / UC Berkeley", "Twin Mars orbiters", "Mars",
          "Two small spacecraft that will fly in formation around Mars to study how the solar wind strips its atmosphere. After launch they waited near Earth, then use an Earth flyby in late 2026 to head for Mars.", MFrame::Sun, { 255, 120, 120, 255 }, day(2025, 11, 13, 20, 55), 0 },
        { "mmx", "MMX", "JAXA", "Mars moons sample return", "Phobos and Deimos",
          "Martian Moons eXploration will study Mars's two moons from orbit and bring a sample from Phobos back to Earth. Scheduled to launch in late 2026; the path shown is JPL's predicted trajectory, so check current launch status.", MFrame::Sun, { 255, 215, 140, 255 }, 0, 0 },
        { "juice", "JUICE", "ESA", "Jupiter icy moons explorer", "Jupiter and Ganymede",
          "Will study Jupiter's three large icy moons and become the first spacecraft to orbit a moon other than our own (Ganymede). Uses a lunar-Earth, Venus and two Earth flybys to reach Jupiter.", MFrame::Sun, { 140, 255, 150, 255 }, day(2023, 4, 14, 12, 14), 0 },
        { "europaclipper", "Europa Clipper", "NASA", "Europa flyby orbiter", "Jupiter and Europa",
          "Will make dozens of close flybys of Jupiter's ocean moon Europa to find out whether it could support life. Gets to Jupiter by way of Mars and Earth.", MFrame::Sun, { 120, 200, 255, 255 }, day(2024, 10, 14, 16, 6), 0 },

        { "apollo11", "Apollo 11 (Saturn V)", "NASA", "First crewed Moon landing", "The Moon",
          "A Saturn V launched Neil Armstrong, Buzz Aldrin and Michael Collins to the Moon. The path shown is the Saturn V's S-IVB third stage after trans-lunar injection (the Apollo spacecraft separated from it and entered lunar orbit). Trajectory data starts after the burn.", MFrame::EarthInertial, { 255, 215, 120, 255 }, day(1969, 7, 16, 13, 32), day(1969, 7, 24, 16, 51) },
        { "artemis1", "Artemis I", "NASA", "Uncrewed lunar test flight", "The Moon",
          "First flight of the Space Launch System and Orion. An uncrewed 25-day trip out to a distant retrograde orbit around the Moon and back. Trajectory data starts after trans-lunar injection.", MFrame::EarthInertial, { 120, 210, 255, 255 }, day(2022, 11, 16, 6, 47), day(2022, 12, 11, 17, 40) },
        { "artemis2", "Artemis II", "NASA", "Crewed lunar flyby", "The Moon",
          "First crewed Artemis flight: a loop around the Moon and back on a free-return trajectory. Trajectory data starts after trans-lunar injection.", MFrame::EarthInertial, { 255, 160, 110, 255 }, 0, 0 },
        { "smile", "SMILE", "ESA / CAS", "Magnetosphere imager", "Earth's magnetosphere",
          "A joint ESA and Chinese Academy of Sciences mission that images how the solar wind interacts with Earth's magnetic field, from a highly elliptical orbit reaching about 120,000 km. Scheduled to launch in 2026; the path shown is JPL's predicted trajectory, so check current launch status.", MFrame::EarthInertial, { 120, 235, 200, 255 }, 0, 0 },
        { "euclid", "Euclid", "ESA", "Dark universe telescope", "Sun-Earth L2",
          "ESA's wide-field telescope for mapping the geometry of the dark universe. Launched on a Falcon 9 and flew to the Sun-Earth L2 point. Shown as the first weeks of its journey.", MFrame::EarthInertial, { 255, 150, 220, 255 }, day(2023, 7, 1, 15, 12), 0 },
        { "roman", "Roman Space Telescope", "NASA", "Wide-field infrared observatory", "Sun-Earth L2",
          "A wide-field infrared survey telescope bound for the Sun-Earth L2 point, to study dark energy and find exoplanets. Scheduled to launch between late 2026 and May 2027; the path shown is a predicted trajectory.", MFrame::EarthInertial, { 190, 170, 255, 255 }, 0, 0 },
        { "jwst", "James Webb Space Telescope", "NASA / ESA / CSA", "Infrared observatory", "Sun-Earth L2",
          "Flew a month-long path to the Sun-Earth L2 point 1.5 million km from Earth, where it unfolded its 6.5 m mirror. Shown as the first weeks of its journey.", MFrame::EarthInertial, { 255, 200, 90, 255 }, day(2021, 12, 25, 12, 20), 0 },
        { "starship", "Starship flight test", "SpaceX", "Suborbital test flight profile (modelled)", "Indian Ocean",
          "A representative profile of the suborbital Starship test flights from Starbase, Texas: ascent over the Gulf of Mexico, a coast above 150 km, then atmospheric reentry and splashdown in the Indian Ocean. This path is modelled from public mission descriptions, not reconstructed from flight telemetry. Dated to Flight 5.", MFrame::EarthFixed, { 230, 235, 245, 255 }, day(2024, 10, 13, 12, 25), day(2024, 10, 13, 13, 30) },
    };
    return t;
}

// Curated milestones. Approximate (month-only) entries are for planned events.
inline const std::vector<Ev>& curatedEvents() {
    static const std::vector<Ev> e = {
        { "voyager1", day(1977, 9, 5), "Launch", false }, { "voyager1", day(1979, 3, 5), "Jupiter closest approach", false },
        { "voyager1", day(1980, 11, 12), "Saturn closest approach", false }, { "voyager1", day(1990, 2, 14), "Pale Blue Dot photo", false },
        { "voyager1", day(2012, 8, 25), "Enters interstellar space", false },
        { "voyager2", day(1977, 8, 20), "Launch", false }, { "voyager2", day(1979, 7, 9), "Jupiter closest approach", false },
        { "voyager2", day(1981, 8, 26), "Saturn closest approach", false }, { "voyager2", day(1986, 1, 24), "Uranus closest approach", false },
        { "voyager2", day(1989, 8, 25), "Neptune closest approach", false }, { "voyager2", day(2018, 11, 5), "Enters interstellar space", false },
        { "pioneer10", day(1972, 3, 3), "Launch", false }, { "pioneer10", day(1973, 12, 3), "Jupiter closest approach", false }, { "pioneer10", day(2003, 1, 23), "Last signal received", false },
        { "pioneer11", day(1973, 4, 6), "Launch", false }, { "pioneer11", day(1974, 12, 3), "Jupiter closest approach", false },
        { "pioneer11", day(1979, 9, 1), "Saturn closest approach (first Saturn flyby)", false }, { "pioneer11", day(1995, 9, 30), "Mission ends", false },
        { "galileo", day(1989, 10, 18), "Launch", false }, { "galileo", day(1991, 10, 29), "Flyby of asteroid Gaspra", false }, { "galileo", day(1993, 8, 28), "Flyby of asteroid Ida", false },
        { "galileo", day(1995, 12, 7), "Arrives at Jupiter; probe enters atmosphere", false }, { "galileo", day(2003, 9, 21), "Plunges into Jupiter", false },
        { "cassini", day(1997, 10, 15), "Launch", false }, { "cassini", day(2004, 7, 1), "Saturn orbit insertion", false },
        { "cassini", day(2005, 1, 14), "Huygens probe lands on Titan", false }, { "cassini", day(2017, 9, 15), "Grand Finale: enters Saturn's atmosphere", false },
        { "stardust", day(1999, 2, 7), "Launch", false }, { "stardust", day(2004, 1, 2), "Flyby of comet Wild 2", false },
        { "stardust", day(2006, 1, 15), "Sample capsule lands on Earth", false }, { "stardust", day(2011, 2, 15), "Flyby of comet Tempel 1", false },
        { "hayabusa", day(2003, 5, 9), "Launch", false }, { "hayabusa", day(2005, 9, 12), "Arrives at asteroid Itokawa", false }, { "hayabusa", day(2010, 6, 13), "Sample capsule returns to Earth", false },
        { "rosetta", day(2004, 3, 2), "Launch", false }, { "rosetta", day(2014, 8, 6), "Arrives at comet 67P", false },
        { "rosetta", day(2014, 11, 12), "Philae lands on the comet", false }, { "rosetta", day(2016, 9, 30), "Controlled landing on the comet; mission ends", false },
        { "messenger", day(2004, 8, 3), "Launch", false }, { "messenger", day(2011, 3, 18), "Enters Mercury orbit", false }, { "messenger", day(2015, 4, 30), "Impacts Mercury", false },
        { "dawn", day(2007, 9, 27), "Launch", false }, { "dawn", day(2011, 7, 16), "Enters orbit around Vesta", false },
        { "dawn", day(2015, 3, 6), "Enters orbit around Ceres", false }, { "dawn", day(2018, 11, 1), "Mission ends", false },
        { "newhorizons", day(2006, 1, 19), "Launch", false }, { "newhorizons", day(2007, 2, 28), "Jupiter gravity assist", false },
        { "newhorizons", day(2015, 7, 14), "Pluto flyby", false }, { "newhorizons", day(2019, 1, 1), "Arrokoth flyby", false },
        { "msl", day(2011, 11, 26), "Launch", false }, { "msl", day(2012, 8, 6), "Curiosity lands in Gale crater", false },
        { "mars2020", day(2020, 7, 30), "Launch", false }, { "mars2020", day(2021, 2, 18), "Perseverance lands in Jezero crater", false },
        { "dart", day(2021, 11, 24), "Launch", false }, { "dart", day(2022, 9, 26), "Impacts Dimorphos", false },
        { "hayabusa2", day(2014, 12, 3), "Launch", false }, { "hayabusa2", day(2018, 6, 27), "Arrives at asteroid Ryugu", false },
        { "hayabusa2", day(2020, 12, 6), "Sample capsule returns to Earth", false },
        { "hayabusa2", day(2026, 7, 15), "Planned flyby of asteroid Torifune", true }, { "hayabusa2", day(2031, 7, 15), "Planned rendezvous with asteroid 1998 KY26", true },
        { "osirisrex", day(2016, 9, 8), "Launch", false }, { "osirisrex", day(2018, 12, 3), "Arrives at asteroid Bennu", false },
        { "osirisrex", day(2023, 9, 24), "Sample capsule lands on Earth", false }, { "osirisrex", day(2029, 4, 15), "Planned rendezvous with asteroid Apophis", true },
        { "juno", day(2011, 8, 5), "Launch", false }, { "juno", day(2016, 7, 5), "Enters Jupiter orbit", false },
        { "parker", day(2018, 8, 12), "Launch", false }, { "parker", day(2024, 12, 24), "Closest approach to the Sun: 6.1 million km", false },
        { "lucy", day(2021, 10, 16), "Launch", false }, { "lucy", day(2025, 4, 20), "Flyby of asteroid Donaldjohanson", false },
        { "lucy", day(2027, 8, 15), "Planned flyby of Trojan asteroid Eurybates", true }, { "lucy", day(2028, 4, 15), "Planned flyby of Trojan asteroid Leucus", true },
        { "lucy", day(2033, 3, 15), "Planned flyby of the Patroclus-Menoetius pair", true },
        { "psyche", day(2023, 10, 13), "Launch", false }, { "psyche", day(2026, 5, 15), "Mars gravity assist", true }, { "psyche", day(2029, 8, 15), "Planned arrival at asteroid Psyche", true },
        { "bepicolombo", day(2018, 10, 20), "Launch", false }, { "bepicolombo", day(2026, 11, 15), "Planned Mercury orbit insertion", true },
        { "juice", day(2023, 4, 14), "Launch", false }, { "juice", day(2024, 8, 20), "Lunar-Earth gravity assist", false },
        { "juice", day(2025, 8, 31), "Venus gravity assist", false }, { "juice", day(2031, 7, 15), "Planned arrival at Jupiter", true }, { "juice", day(2034, 12, 15), "Planned entry into Ganymede orbit", true },
        { "europaclipper", day(2024, 10, 14), "Launch", false }, { "europaclipper", day(2025, 3, 1), "Mars gravity assist", false },
        { "europaclipper", day(2026, 12, 3), "Earth gravity assist", true }, { "europaclipper", day(2030, 4, 15), "Planned arrival at Jupiter", true },
        { "ulysses", day(1990, 10, 6), "Launch", false }, { "ulysses", day(1992, 2, 8), "Jupiter gravity assist", false },
        { "ulysses", day(1994, 9, 13), "First pass over the Sun's south pole", false }, { "ulysses", day(1995, 7, 31), "First pass over the Sun's north pole", false },
        { "ulysses", day(2009, 6, 30), "Mission ends", false },
        { "solarorbiter", day(2020, 2, 10), "Launch", false },
        { "hera", day(2024, 10, 7), "Launch", false }, { "hera", day(2026, 12, 1), "Planned arrival at Didymos", true },
        { "escapade", day(2025, 11, 13), "Launch on New Glenn", false }, { "escapade", day(2027, 9, 15), "Planned Mars orbit insertion", true },
        { "mmx", day(2026, 10, 20), "Planned launch", true }, { "mmx", day(2027, 9, 15), "Planned arrival at Mars", true }, { "mmx", day(2031, 7, 15), "Planned return of Phobos sample", true },
        { "apollo11", day(1969, 7, 16, 13, 32), "Liftoff from Cape Kennedy", false }, { "apollo11", day(1969, 7, 16, 16, 22), "Trans-lunar injection burn", false },
        { "apollo11", day(1969, 7, 19, 17, 21), "Enters lunar orbit", false }, { "apollo11", day(1969, 7, 20, 20, 17), "Eagle lands in the Sea of Tranquility", false },
        { "apollo11", day(1969, 7, 21, 17, 54), "Eagle lifts off from the Moon", false }, { "apollo11", day(1969, 7, 22, 4, 55), "Trans-earth injection burn", false },
        { "apollo11", day(1969, 7, 24, 16, 51), "Splashdown in the Pacific", false },
        { "artemis1", day(2022, 11, 16, 6, 47), "Liftoff of SLS from Kennedy Space Center", false }, { "artemis1", day(2022, 11, 21), "Closest lunar flyby (130 km)", false },
        { "artemis1", day(2022, 11, 25), "Enters distant retrograde orbit", false }, { "artemis1", day(2022, 12, 11, 17, 40), "Splashdown in the Pacific", false },
        { "euclid", day(2023, 7, 1, 15, 12), "Liftoff on Falcon 9 from Cape Canaveral", false }, { "euclid", day(2023, 7, 28), "Arrives at the Sun-Earth L2 point", false },
        { "jwst", day(2021, 12, 25, 12, 20), "Liftoff on Ariane 5 from Kourou", false }, { "jwst", day(2022, 1, 8), "Mirror and sunshield deployed", false },
        { "jwst", day(2022, 1, 24), "Arrives at the Sun-Earth L2 point", false },
    };
    return e;
}

// Representative Starship flight: launched from Starbase on an east-south-east azimuth, it flies about 195 degrees
// around the Earth (inertial great circle) while the Earth turns underneath, ending in the Indian Ocean.
struct StarKey { double t, progress, altKm; };

inline void buildStarship(Mission& m) {
    static const StarKey keys[] = {
        { 0, 0.0, 0 }, { 60, 0.001, 9 }, { 160, 0.006, 70 }, { 300, 0.025, 110 }, { 510, 0.095, 145 }, { 1200, 0.33, 160 }, { 1900, 0.56, 150 },
        { 2500, 0.76, 110 }, { 2850, 0.865, 75 }, { 3100, 0.93, 48 }, { 3500, 0.98, 15 }, { 3900, 1.0, 0 },
    };
    const double R = 6371.0, startLat = 25.9972 * DEG, startLon = -97.1566 * DEG, azimuth = 100.0 * DEG, totalArc = 195.0 * DEG, earthSpin = 7.2921159e-5;
    auto at = [&](double tt) {
        size_t i = 0;
        while (i + 2 < sizeof(keys) / sizeof(keys[0]) && tt > keys[i + 1].t) ++i;
        const StarKey &k0 = keys[i], &k1 = keys[i + 1];
        const double f = std::max(0.0, std::min(1.0, (tt - k0.t) / (k1.t - k0.t)));
        const double arc = (k0.progress + (k1.progress - k0.progress) * f) * totalArc, alt = k0.altKm + (k1.altKm - k0.altKm) * f;
        // Direct geodesic problem on a sphere: where do we get to after flying `arc` from Starbase on this azimuth?
        const double lat = std::asin(std::sin(startLat) * std::cos(arc) + std::cos(startLat) * std::sin(arc) * std::cos(azimuth));
        const double inertialLon = startLon + std::atan2(std::sin(azimuth) * std::sin(arc) * std::cos(startLat), std::cos(arc) - std::sin(startLat) * std::sin(lat));
        const double lon = inertialLon - earthSpin * tt;  // the ground track moves with the rotating Earth
        return Vec3d{ (R + alt) * std::cos(lat) * std::cos(lon), (R + alt) * std::cos(lat) * std::sin(lon), (R + alt) * std::sin(lat) };
    };
    const double dt = 10.0;
    for (double tt = 0; tt <= 3900.0; tt += dt) {
        const Vec3d p = at(tt), p0 = at(tt - 1.0), p1 = at(tt + 1.0);
        m.t.push_back(m.launch + tt);
        m.s.push_back({ p.x, p.y, p.z, (p1.x - p0.x) / 2.0, (p1.y - p0.y) / 2.0, (p1.z - p0.z) / 2.0 });
    }
    const struct { double t; const char* label; } ev[] = {
        { 0, "Liftoff from Starbase, Texas" }, { 70, "Max-Q: peak aerodynamic pressure" }, { 160, "Hot staging: Super Heavy separates" },
        { 510, "Ship engine cutoff (about 7 km/s)" }, { 1900, "Coasting near apogee above 150 km" }, { 2850, "Reentry: plasma heating begins" },
        { 3500, "Flip and landing burn" }, { 3900, "Splashdown in the Indian Ocean" },
    };
    for (const auto& e : ev) m.events.push_back({ m.launch + e.t, e.label, false });
}

inline bool loadTrajectory(Mission& m, const std::string& path, bool inAuDays) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    const double vScale = inAuDays ? 1.0 / 86400.0 : 1.0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        double v[7];
        const char* p = line.c_str();
        char* end = nullptr;
        bool ok = true;
        for (int k = 0; k < 7; ++k) {
            v[k] = std::strtod(p, &end);
            if (end == p) { ok = false; break; }
            p = end;
            if (*p == ',') ++p;
        }
        if (!ok) continue;
        m.t.push_back(v[0]);
        m.s.push_back({ v[1], v[2], v[3], v[4] * vScale, v[5] * vScale, v[6] * vScale });
    }
    return m.loaded();
}

}  // namespace missiondb

inline std::vector<Mission> loadMissions() {
    std::vector<Mission> out;
    // Generated flybys: key,unix,label,closest approach (AU)
    std::vector<std::pair<std::string, MEvent>> generated;
    {
        std::ifstream f(paths::assetPath("assets/missions/events.csv"));
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t c1 = line.find(','), c2 = c1 == std::string::npos ? c1 : line.find(',', c1 + 1), c3 = c2 == std::string::npos ? c2 : line.find(',', c2 + 1);
            if (c3 == std::string::npos) continue;
            const std::string label = line.substr(c2 + 1, c3 - c2 - 1);
            const double closest = std::atof(line.c_str() + c3 + 1);
            // Inner-planet gravity assists pass within a few thousand km; anything farther is just a spacecraft cruising nearby.
            const bool giant = label.find("Jupiter") == 0 || label.find("Saturn") == 0 || label.find("Uranus") == 0 || label.find("Neptune") == 0;
            if (!giant && closest > 0.0045) continue;
            generated.push_back({ line.substr(0, c1), { std::atof(line.substr(c1 + 1, c2 - c1 - 1).c_str()), label, false } });
        }
    }
    for (const missiondb::Meta& meta : missiondb::table()) {
        Mission m;
        m.key = meta.key; m.name = meta.name; m.agency = meta.agency; m.kind = meta.kind; m.target = meta.target; m.summary = meta.summary;
        m.frame = meta.frame; m.color = meta.color; m.launch = meta.launch; m.end = meta.end;
        if (m.key == "starship") {
            missiondb::buildStarship(m);
        } else if (!missiondb::loadTrajectory(m, paths::assetPath("assets/missions/" + m.key + ".csv"), m.frame == MFrame::Sun)) {
            TraceLog(LOG_WARNING, "Mission %s: trajectory file missing", m.key.c_str());
            continue;
        }
        if (m.launch == 0) { m.launch = m.tStart(); m.launchKnown = false; }
        if (m.key == "artemis2") m.end = m.tEnd();
        if (m.key == "mmx" || m.key == "roman" || m.key == "smile") m.plannedOverride = true;
        for (const missiondb::Ev& e : missiondb::curatedEvents()) if (m.key == e.key) m.events.push_back({ e.t, e.label, e.approx });
        const size_t curated = m.events.size();
        for (const auto& g : generated) {
            if (g.first != m.key) continue;
            bool near = false;
            for (size_t i = 0; i < curated; ++i) if (std::fabs(m.events[i].t - g.second.t) < 4.0 * 86400.0) { near = true; break; }
            if (!near) m.events.push_back(g.second);
        }
        std::sort(m.events.begin(), m.events.end(), [](const MEvent& a, const MEvent& b) { return a.t < b.t; });
        out.push_back(std::move(m));
    }
    TraceLog(LOG_INFO, "Missions: %zu trajectories loaded", out.size());
    return out;
}

inline std::string fmtMissionDate(double t, bool approx) {
    return formatTime(t, approx ? "%b %Y" : "%Y-%m-%d");
}

// Elapsed mission time, e.g. "12 y 3 mo" or "4 d 6 h" or "T+ 00:12:30".
inline std::string fmtDuration(double seconds) {
    char b[48];
    const double a = std::fabs(seconds);
    if (a >= 2.0 * 365.25 * 86400.0) std::snprintf(b, sizeof b, "%.1f years", a / (365.25 * 86400.0));
    else if (a >= 60.0 * 86400.0) std::snprintf(b, sizeof b, "%.0f days", a / 86400.0);
    else if (a >= 86400.0) std::snprintf(b, sizeof b, "%.1f days", a / 86400.0);
    else if (a >= 3600.0) std::snprintf(b, sizeof b, "%.1f hours", a / 3600.0);
    else std::snprintf(b, sizeof b, "%.0f min", a / 60.0);
    return b;
}

// Greedy word wrap for panel text.
inline std::vector<std::string> wrapText(const std::string& text, int maxChars) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flush = [&]() {
        if (word.empty()) return;
        if (!cur.empty() && (int)(cur.size() + 1 + word.size()) > maxChars) { lines.push_back(cur); cur.clear(); }
        if (!cur.empty()) cur += ' ';
        cur += word;
        word.clear();
    };
    for (char ch : text) { if (ch == ' ') flush(); else word += ch; }
    flush();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}
