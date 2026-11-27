#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "catalog.hpp"
#include "orbit.hpp"

inline Vec3d moonPos(double du) {
    const double Lp = (218.316 + 13.176396 * du) * DEG, Mp = (134.963 + 13.064993 * du) * DEG,
                 F = (93.272 + 13.229350 * du) * DEG;
    const double lon = Lp + 6.289 * DEG * std::sin(Mp), lat = 5.128 * DEG * std::sin(F);
    const double r = 385001.0 - 20905.0 * std::cos(Mp), eps = (23.439 - 4e-7 * du) * DEG;
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

struct Pass { double aos, los, maxEl; bool vis; };

inline std::vector<Pass> computePasses(const Entity& e, const Station& g, double t0, double R) {
    std::vector<Pass> out;
    const double step = 20.0, horizon = 24.0 * 3600.0;
    bool in = false;
    Pass cur{};
    for (double t = t0; t <= t0 + horizon; t += step) {
        const Vec3d sp = e.orbit.positionAt(t);
        const Look l = lookAngles(g, sp, eraAt(t), R);
        if (l.el > 0) {
            if (!in) { in = true; cur = { t, t, l.el, false }; }
            cur.los = t; cur.maxEl = std::max(cur.maxEl, l.el);

            const Vec3d sd = sunDirEci(t), o = stationEci(g, eraAt(t), 1.0);
            if (sunlit(sp, sd, R) && o.x * sd.x + o.y * sd.y + o.z * sd.z < -0.1045) cur.vis = true;
        } else if (in) {
            in = false; out.push_back(cur);
            if (out.size() >= 5) return out;
        }
    }
    if (in) out.push_back(cur);
    return out;
}
