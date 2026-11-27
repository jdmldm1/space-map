#pragma once
#include <cmath>
#include "orbit.hpp"

struct PlanetDef {
    const char* name;
    double a0, ad, e0, ed, i0, id, L0, Ld, w0, wd, O0, Od;
    double radiusKm;
    unsigned char r, g, b;
};

static const PlanetDef PLANETS[] = {
    { "Mercury", 0.38709927, 0.00000037, 0.20563593, 0.00001906, 7.00497902, -0.00594749, 252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081, 2439.7, 170, 165, 160 },
    { "Venus", 0.72333566, 0.00000390, 0.00677672, -0.00004107, 3.39467605, -0.00078890, 181.97909950, 58517.81538729, 131.60246718, 0.00268329, 76.67984255, -0.27769418, 6051.8, 240, 215, 150 },
    { "Earth", 1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668, 100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0, 6371.0, 90, 150, 255 },
    { "Mars", 1.52371034, 0.00001847, 0.09339410, 0.00007882, 1.84969142, -0.00813131, -4.55343205, 19140.30268499, -23.94362959, 0.44441088, 49.55953891, -0.29257343, 3389.5, 230, 120, 80 },
    { "Jupiter", 5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714, 34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106, 69911.0, 225, 190, 150 },
    { "Saturn", 9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609, 49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794, 58232.0, 235, 215, 160 },
    { "Uranus", 19.18916464, -0.00196176, 0.04725744, -0.00004397, 0.77263783, -0.00242939, 313.23810451, 428.48202785, 170.95427630, 0.40805281, 74.01692503, 0.04240589, 25362.0, 150, 220, 230 },
    { "Neptune", 30.06992276, 0.00026291, 0.00859048, 0.00005105, 1.77004347, 0.00035372, -55.12002969, 218.45945325, 44.96476227, -0.32241464, 131.78422574, -0.00508664, 24622.0, 90, 120, 255 },
    { "Pluto", 39.48211675, -0.00031596, 0.24882730, 0.00005170, 17.14001206, 0.00004818, 238.92903833, 145.20780515, 224.06891629, -0.04062942, 110.30393684, -0.01183482, 1188.3, 200, 180, 160 },
};
static const int NPLANETS = (int)(sizeof(PLANETS) / sizeof(PLANETS[0]));
static const double SUN_RADIUS_KM = 695700.0, AU_KM = 149597870.7, LIGHT_AU_PER_MIN = 0.1199169832;

struct KeplerEl { double a, e, iDeg, nodeDeg, argpDeg, MDeg; };

static double solveKepler(double Mrad, double e) {
    double M = std::fmod(Mrad, 2.0 * PI_D);
    if (M > PI_D) M -= 2.0 * PI_D; else if (M < -PI_D) M += 2.0 * PI_D;
    double E = e < 0.8 ? M : PI_D;
    for (int k = 0; k < 30; ++k) {
        const double d = (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
        E -= d;
        if (std::fabs(d) < 1e-11) break;
    }
    return E;
}

static Vec3d eclipticToEquatorial(const Vec3d& p) {
    const double eps = 23.43928 * DEG, c = std::cos(eps), s = std::sin(eps);
    return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
}

static Vec3d keplerEclipticE(const KeplerEl& k, double E) {
    const double xp = k.a * (std::cos(E) - k.e), yp = k.a * std::sqrt(1.0 - k.e * k.e) * std::sin(E);
    const double w = k.argpDeg * DEG, O = k.nodeDeg * DEG, I = k.iDeg * DEG;
    const double cw = std::cos(w), sw = std::sin(w), cO = std::cos(O), sO = std::sin(O), cI = std::cos(I), sI = std::sin(I);
    return { (cw * cO - sw * sO * cI) * xp + (-sw * cO - cw * sO * cI) * yp,
             (cw * sO + sw * cO * cI) * xp + (-sw * sO + cw * cO * cI) * yp,
             (sw * sI) * xp + (cw * sI) * yp };
}
static Vec3d keplerEcliptic(const KeplerEl& k) { return keplerEclipticE(k, solveKepler(k.MDeg * DEG, k.e)); }

static KeplerEl planetElements(int i, double T) {
    const PlanetDef& p = PLANETS[i];
    KeplerEl k;
    k.a = p.a0 + p.ad * T; k.e = p.e0 + p.ed * T; k.iDeg = p.i0 + p.id * T;
    const double L = p.L0 + p.Ld * T, varpi = p.w0 + p.wd * T;
    k.nodeDeg = p.O0 + p.Od * T;
    k.argpDeg = varpi - k.nodeDeg;
    k.MDeg = L - varpi;
    return k;
}

static Vec3d planetPos(int i, double T) { return eclipticToEquatorial(keplerEcliptic(planetElements(i, T))); }

static Vec3d orbitPoint(const KeplerEl& el, double frac) { return eclipticToEquatorial(keplerEclipticE(el, frac * 2.0 * PI_D)); }

struct SmallBody { const char* name; KeplerEl el; double periodYears; };
static SmallBody halley() {
    const double a = 17.834, nDegPerDay = 0.9856076686 / std::pow(a, 1.5);
    const double daysSincePeri = 2451545.0 - 2446470.96;
    return { "Halley", { a, 0.96714, 162.26, 58.42, 111.33, std::fmod(nDegPerDay * daysSincePeri, 360.0) }, std::pow(a, 1.5) };
}
static Vec3d smallBodyPos(const SmallBody& b, double T) {
    KeplerEl k = b.el;
    k.MDeg += 360.0 * (T * 36525.0 / 365.25) / b.periodYears;
    return eclipticToEquatorial(keplerEcliptic(k));
}
