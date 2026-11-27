#pragma once
// A rough orbital-lifetime estimate for a near-circular low Earth orbit, from the TLE's B* drag term and a mean
// solar-activity atmosphere. It is only good to a factor of two or three (real lifetimes swing with the solar cycle
// and with how the satellite is oriented), so the UI labels it as an estimate.
#include <cmath>
#include <cstdio>
#include <string>

// Mean-activity atmospheric density (kg/m^3), log-interpolated between table rows.
inline double atmosphereDensity(double altKm) {
    static const double h[] = { 150, 200, 250, 300, 350, 400, 450, 500, 600, 700, 800 };
    static const double rho[] = { 2.07e-9, 2.54e-10, 6.07e-11, 1.92e-11, 7.03e-12, 2.80e-12, 1.18e-12, 5.21e-13, 1.14e-13, 3.07e-14, 1.136e-14 };
    const int n = (int)(sizeof(h) / sizeof(h[0]));
    if (altKm <= h[0]) return rho[0];
    if (altKm >= h[n - 1]) return rho[n - 1];
    int i = 0;
    while (altKm > h[i + 1]) ++i;
    const double f = (altKm - h[i]) / (h[i + 1] - h[i]);
    return std::exp(std::log(rho[i]) * (1.0 - f) + std::log(rho[i + 1]) * f);
}

// Days until the orbit decays to 150 km, or -1 when there is no usable estimate, or -2 beyond 100 years.
inline double estimateLifetimeDays(double meanAltKm, double bstar) {
    if (!(bstar > 0.0) || meanAltKm < 150.0 || meanAltKm > 800.0) return -1.0;
    const double ballistic = 84.0 * bstar;  // Cd*A/m in m^2/kg, calibrated so the ISS's B* of ~1e-4 gives its ~0.008
    const double mu = 3.986004418e14;
    double days = 0.0;
    for (double alt = meanAltKm; alt > 150.0; alt -= 2.0) {
        const double a = (6378.137 + alt) * 1000.0;
        const double rate = ballistic * atmosphereDensity(alt) * std::sqrt(mu * a);  // da/dt in m/s
        if (rate <= 0.0) return -2.0;
        days += 2000.0 / rate / 86400.0;
        if (days > 36525.0) return -2.0;
    }
    return days;
}

inline std::string lifetimeText(double days) {
    char b[64];
    if (days < 0.0) return days < -1.5 ? "over 100 years" : "";
    if (days < 2.0) std::snprintf(b, sizeof b, "about %.0f hours", days * 24.0);
    else if (days < 90.0) std::snprintf(b, sizeof b, "about %.0f days", days);
    else if (days < 700.0) std::snprintf(b, sizeof b, "about %.0f months", days / 30.4);
    else std::snprintf(b, sizeof b, "about %.0f years", days / 365.25);
    return b;
}
