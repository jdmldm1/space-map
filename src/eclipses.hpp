#pragma once
// Eclipse prediction from the app's low-precision Sun and Moon positions. Good for dates (within about an hour); the
// type of marginal eclipses can be off because the Moon formula is only accurate to a fraction of a degree.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "observer.hpp"

struct Eclipse {
    double time = 0;       // unix seconds at greatest eclipse
    bool solar = false;
    std::string kind;      // "Total", "Annular", "Partial", "Penumbral"
    double separation = 0; // degrees between the Moon and the Sun (solar) or the Earth's shadow axis (lunar)
};

namespace eclipse_detail {
inline double angleBetween(const Vec3d& a, const Vec3d& b) {
    const double la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z), lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    const double c = (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb);
    return std::acos(std::max(-1.0, std::min(1.0, c)));
}
// Angular separation from the Sun (solar = true) or from the point opposite the Sun (lunar), in radians.
inline double separation(double t, bool solar, double* moonRadius = nullptr) {
    const double days = t / 86400.0 - 10957.5;
    const Vec3d m = moonPos(days), s = sunDirEci(t);
    if (moonRadius) *moonRadius = std::asin(1737.4 / std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z));
    return solar ? angleBetween(m, s) : angleBetween(m, { -s.x, -s.y, -s.z });
}
}  // namespace eclipse_detail

inline std::vector<Eclipse> findEclipses(double from, double years, size_t maxCount = 12) {
    using namespace eclipse_detail;
    std::vector<Eclipse> out;
    const double step = 3600.0, end = from + years * 365.25 * 86400.0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool solar = pass == 0;
        double prev2 = separation(from - 2 * step, solar), prev1 = separation(from - step, solar);
        for (double t = from; t < end; t += step) {
            const double cur = separation(t, solar);
            if (prev1 < prev2 && prev1 <= cur && prev1 < 2.2 * DEG) {  // local minimum at t - step
                double lo = t - 2 * step, hi = t, best = t - step;
                for (int k = 0; k < 40; ++k) {  // ternary search to the minute
                    const double a = lo + (hi - lo) / 3.0, b = hi - (hi - lo) / 3.0;
                    if (separation(a, solar) < separation(b, solar)) hi = b; else lo = a;
                }
                best = 0.5 * (lo + hi);
                double rm = 0;
                const double sep = separation(best, solar, &rm) / DEG, rmDeg = rm / DEG;
                Eclipse e;
                e.time = best; e.solar = solar; e.separation = sep;
                if (solar) {
                    const double rs = 0.2666, parallax = 0.95;  // Sun's apparent radius and the Moon's horizontal parallax, degrees
                    if (sep < parallax - 0.05) e.kind = rmDeg >= rs ? "Total" : "Annular";  // shadow axis crosses the Earth
                    else if (sep < rmDeg + rs + parallax) e.kind = "Partial";
                } else {
                    const double umbra = 0.70, penumbra = 1.28;
                    if (sep < umbra - rmDeg) e.kind = "Total";
                    else if (sep < umbra + rmDeg) e.kind = "Partial";
                    else if (sep < penumbra + rmDeg) e.kind = "Penumbral";
                }
                if (!e.kind.empty()) out.push_back(e);
            }
            prev2 = prev1; prev1 = cur;
        }
    }
    std::sort(out.begin(), out.end(), [](const Eclipse& a, const Eclipse& b) { return a.time < b.time; });
    if (out.size() > maxCount) out.resize(maxCount);
    return out;
}
