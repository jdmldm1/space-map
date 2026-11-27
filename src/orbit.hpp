#pragma once
#include <cmath>
#include "sgp4.hpp"

constexpr double PI_D = 3.14159265358979323846;
constexpr double DEG = PI_D / 180.0;

struct Vec3d { double x, y, z; };

struct Orbit {
    double a = 7000;
    double e = 0;
    double inc = 0;
    double raan = 0;
    double argp = 0;
    double M0 = 0;
    double mu = 398600.4418;
    double epoch = 0;
    double raanDot = 0;
    double argpDot = 0;
    double tleN = 0;
    bool useSgp4 = false;
    Sgp4 sg;

    void enableSgp4(double bstar) {
        if (tleN > 0) useSgp4 = sg.init(tleN * 60.0, e, inc, raan, argp, M0, bstar);
    }

    static Orbit make(double a, double e, double incDeg, double raanDeg,
                      double argpDeg, double M0Deg, double mu = 398600.4418) {
        Orbit orbit;
        orbit.a = a;
        orbit.e = e;
        orbit.inc = incDeg * DEG;
        orbit.raan = raanDeg * DEG;
        orbit.argp = argpDeg * DEG;
        orbit.M0 = M0Deg * DEG;
        orbit.mu = mu;
        return orbit;
    }
    static Orbit circular(double radiusKm, double incDeg, double raanDeg,
                          double M0Deg, double mu = 398600.4418) {
        return make(radiusKm, 0.0, incDeg, raanDeg, 0.0, M0Deg, mu);
    }

    double meanMotion() const { return std::sqrt(mu / (a * a * a)); }
    double period() const { return 2.0 * PI_D / meanMotion(); }

    Vec3d positionFromMean(double M, double dt = 0.0) const {
        M = std::fmod(M, 2.0 * PI_D);
        double E = (e < 0.8) ? M : PI_D;
        for (int k = 0; k < 12; ++k) {
            const double d = (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
            E -= d;
            if (std::fabs(d) < 1e-10) break;
        }
        const double xp = a * (std::cos(E) - e);
        const double yp = a * std::sqrt(1.0 - e * e) * std::sin(E);
        const double rn = raan + raanDot * dt, wp = argp + argpDot * dt;
        const double cO = std::cos(rn), sO = std::sin(rn);
        const double ci = std::cos(inc),  si = std::sin(inc);
        const double cw = std::cos(wp), sw = std::sin(wp);
        return {
            (cO * cw - sO * sw * ci) * xp + (-cO * sw - sO * cw * ci) * yp,
            (sO * cw + cO * sw * ci) * xp + (-sO * sw + cO * cw * ci) * yp,
            (sw * si) * xp + (cw * si) * yp };
    }
    Vec3d positionAt(double t) const {
        const double dt = t - epoch;
        if (useSgp4) {
            double r[3];
            if (sg.propagate(dt / 60.0, r)) return { r[0], r[1], r[2] };
        }
        return positionFromMean(M0 + meanMotion() * dt, dt);
    }

    static Orbit fromTle(double incDeg, double raanDeg, double ecc, double argpDeg, double MDeg,
                         double revPerDay, double epochUnix, double mu = 398600.4418) {
        const double n = revPerDay * 2.0 * PI_D / 86400.0;
        const double a = std::cbrt(mu / (n * n));
        Orbit o = make(a, ecc, incDeg, raanDeg, argpDeg, MDeg, mu);
        o.epoch = epochUnix;
        o.tleN = n;
        const double J2 = 1.08262668e-3, Re = 6378.137;
        const double p = a * (1.0 - ecc * ecc);
        const double k = 1.5 * J2 * (Re / p) * (Re / p) * n;
        const double si = std::sin(o.inc);
        o.raanDot = -k * std::cos(o.inc);
        o.argpDot = k * (2.0 - 2.5 * si * si);
        return o;
    }

    double speedAt(double r) const { return std::sqrt(mu * (2.0 / r - 1.0 / a)); }
};
