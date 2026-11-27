#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "catalog.hpp"
#include "orbit.hpp"

// Geocentric Moon position (km, equatorial J2000-ish). Principal terms of the lunar theory (Meeus): about 0.1 degree.
inline Vec3d moonPos(double du) {
    const double Lp = (218.3165 + 13.17639648 * du) * DEG, D = (297.8502 + 12.19074912 * du) * DEG, M = (357.5291 + 0.98560028 * du) * DEG;
    const double Mp = (134.9634 + 13.06499295 * du) * DEG, F = (93.2721 + 13.22935025 * du) * DEG;
    const double lonDeg = 6.289 * std::sin(Mp) + 1.274 * std::sin(2 * D - Mp) + 0.658 * std::sin(2 * D) + 0.214 * std::sin(2 * Mp) - 0.186 * std::sin(M) - 0.114 * std::sin(2 * F) +
                          0.059 * std::sin(2 * D - 2 * Mp) + 0.057 * std::sin(2 * D - M - Mp) + 0.053 * std::sin(2 * D + Mp) + 0.046 * std::sin(2 * D - M) + 0.041 * std::sin(Mp - M) -
                          0.035 * std::sin(D) - 0.030 * std::sin(Mp + M);
    const double latDeg = 5.128 * std::sin(F) + 0.2806 * std::sin(Mp + F) + 0.2777 * std::sin(Mp - F) + 0.1732 * std::sin(2 * D - F) + 0.0554 * std::sin(2 * D - Mp + F) +
                          0.0463 * std::sin(2 * D - Mp - F) + 0.0326 * std::sin(2 * D + F) + 0.0172 * std::sin(2 * Mp + F);
    const double lon = Lp + lonDeg * DEG, lat = latDeg * DEG;
    const double r = 385001.0 - 20905.0 * std::cos(Mp) - 3699.0 * std::cos(2 * D - Mp) - 2956.0 * std::cos(2 * D) - 570.0 * std::cos(2 * Mp), eps = (23.439 - 4e-7 * du) * DEG;
    const double cb = std::cos(lat), sb = std::sin(lat), cl = std::cos(lon), sl = std::sin(lon);
    return { r * cb * cl, r * (cb * sl * std::cos(eps) - sb * std::sin(eps)), r * (cb * sl * std::sin(eps) + sb * std::cos(eps)) };
}

inline Vec3d sunDirEci(double t) {
    const double du = t / 86400.0 - 10957.5;
    const double L = std::fmod(280.460 + 0.9856474 * du, 360.0), g = (357.528 + 0.9856003 * du) * DEG;
    const double lam = (L + 1.915 * std::sin(g) + 0.020 * std::sin(2.0 * g)) * DEG, eps = (23.439 - 4e-7 * du) * DEG;
    return { std::cos(lam), std::sin(lam) * std::cos(eps), std::sin(lam) * std::sin(eps) };
}

inline bool sunlit(const Vec3d& p, const Vec3d& s, double R) {
    const double d = p.x * s.x + p.y * s.y + p.z * s.z;
    if (d >= 0.0) return true;
    return p.x * p.x + p.y * p.y + p.z * p.z - d * d > R * R;
}

struct Station { bool set = false; double lat = 0, lon = 0; };

inline double eraAt(double t) {
    const double du = t / 86400.0 - 10957.5;
    return 2.0 * PI_D * std::fmod(0.7790572732640 + 1.00273781191135448 * du, 1.0);
}

inline Vec3d stationEci(const Station& g, double era, double R) {
    const double la = g.lat * DEG, lo = g.lon * DEG + era;
    return { R * std::cos(la) * std::cos(lo), R * std::cos(la) * std::sin(lo), R * std::sin(la) };
}

struct Look { double el, az, range; };

inline Look lookAngles(const Station& g, const Vec3d& sat, double era, double R) {
    const Vec3d o = stationEci(g, era, R);
    const Vec3d r{ sat.x - o.x, sat.y - o.y, sat.z - o.z };
    const double on = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z);
    const Vec3d up{ o.x / on, o.y / on, o.z / on };
    const double hn = std::max(std::hypot(up.x, up.y), 1e-9);
    const Vec3d east{ -up.y / hn, up.x / hn, 0.0 };
    const Vec3d north{ up.y * east.z - up.z * east.y, up.z * east.x - up.x * east.z, up.x * east.y - up.y * east.x };
    const double rng = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    Look l;
    l.range = rng;
    l.el = std::asin(std::max(-1.0, std::min(1.0, (r.x * up.x + r.y * up.y + r.z * up.z) / rng)));
    l.az = std::atan2(r.x * east.x + r.y * east.y + r.z * east.z, r.x * north.x + r.y * north.y + r.z * north.z);
    if (l.az < 0) l.az += 2.0 * PI_D;
    return l;
}

struct Pass { double aos, los, maxEl; bool vis; double peak = 0, aosAz = 0, peakAz = 0, losAz = 0; };

// Passes of `e` above the horizon, starting at t0. Rise and set times are refined by bisection to about a second, and
// passes that never climb above minElRad are dropped. "vis" means the satellite is sunlit while the observer is in twilight or darkness.
inline std::vector<Pass> computePasses(const Entity& e, const Station& g, double t0, double R, double horizonHours = 24.0, size_t maxPasses = 5, double minElRad = 0.0) {
    std::vector<Pass> out;
    const double step = horizonHours > 48.0 ? 30.0 : 20.0, horizon = horizonHours * 3600.0;
    auto lookAt = [&](double t) { return lookAngles(g, e.orbit.positionAt(t), eraAt(t), R); };
    auto crossing = [&](double a, double b) {  // el(a) and el(b) are on opposite sides of the horizon
        const bool aboveA = lookAt(a).el > 0;
        for (int k = 0; k < 7 && b - a > 1.0; ++k) {
            const double m = 0.5 * (a + b);
            if ((lookAt(m).el > 0) == aboveA) a = m; else b = m;
        }
        return 0.5 * (a + b);
    };
    bool in = false;
    Pass cur{};
    double prevT = t0;
    double prevEl = lookAt(t0).el;
    for (double t = t0; t <= t0 + horizon; t += step) {
        const Look l = lookAt(t);
        if (l.el > 0) {
            if (!in) {
                in = true;
                const double aos = t == t0 ? t0 : crossing(prevT, t);
                cur = { aos, t, l.el, false, t, lookAt(aos).az, l.az, 0 };
            }
            cur.los = t;
            if (l.el > cur.maxEl) { cur.maxEl = l.el; cur.peak = t; cur.peakAz = l.az; }
            const Vec3d sd = sunDirEci(t), sp = e.orbit.positionAt(t), o = stationEci(g, eraAt(t), 1.0);
            if (sunlit(sp, sd, R) && o.x * sd.x + o.y * sd.y + o.z * sd.z < -0.1045) cur.vis = true;
        } else if (in) {
            in = false;
            cur.los = crossing(prevT, t);
            cur.losAz = lookAt(cur.los).az;
            if (cur.maxEl >= minElRad) out.push_back(cur);
            if (out.size() >= maxPasses) return out;
        }
        prevT = t; prevEl = l.el;
    }
    (void)prevEl;
    if (in) { cur.losAz = lookAt(cur.los).az; if (cur.maxEl >= minElRad) out.push_back(cur); }
    return out;
}
