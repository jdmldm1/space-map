#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

struct StarRec { std::string name; float ra, dec, dist, mag, bv; };

inline std::vector<StarRec> loadStars(const std::string& path) {
    std::vector<StarRec> v;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> c;
        size_t b = 0;
        for (size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == ',') { c.push_back(line.substr(b, i - b)); b = i + 1; }
        if (c.size() < 6) continue;
        v.push_back({ c[0], (float)std::atof(c[1].c_str()), (float)std::atof(c[2].c_str()), (float)std::atof(c[3].c_str()),
                      (float)std::atof(c[4].c_str()), (float)std::atof(c[5].c_str()) });
    }
    return v;
}

inline void eqToGal(double x, double y, double z, double& gx, double& gy, double& gz) {
    gx = -0.0548755604 * x - 0.8734370902 * y - 0.4838350155 * z;
    gy = 0.4941094279 * x - 0.4448296300 * y + 0.7469822445 * z;
    gz = -0.8676661490 * x - 0.1980763734 * y + 0.4559837762 * z;
}
inline void galToEq(double gx, double gy, double gz, double& x, double& y, double& z) {
    x = -0.0548755604 * gx + 0.4941094279 * gy - 0.8676661490 * gz;
    y = -0.8734370902 * gx - 0.4448296300 * gy - 0.1980763734 * gz;
    z = -0.4838350155 * gx + 0.7469822445 * gy + 0.4559837762 * gz;
}

struct Constellation {
    std::string name, english;
    float labelRa = 0, labelDec = 0;
    std::vector<std::array<float, 4>> segs;
};

inline std::vector<Constellation> loadConstellations(const std::string& path) {
    std::vector<Constellation> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.back() == '\r') line.pop_back();
        std::vector<std::string> parts;
        size_t b = 0;
        for (size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == '|') { parts.push_back(line.substr(b, i - b)); b = i + 1; }
        if (parts.size() < 4) continue;
        Constellation c;
        c.name = parts[0]; c.english = parts[1];
        std::sscanf(parts[2].c_str(), "%f,%f", &c.labelRa, &c.labelDec);
        size_t s0 = 0;
        const std::string& sg = parts[3];
        for (size_t i = 0; i <= sg.size(); ++i)
            if (i == sg.size() || sg[i] == ';') {
                std::array<float, 4> a{};
                if (std::sscanf(sg.substr(s0, i - s0).c_str(), "%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3]) == 4) c.segs.push_back(a);
                s0 = i + 1;
            }
        out.push_back(std::move(c));
    }
    return out;
}

struct Rgb { unsigned char r, g, b; };
inline Rgb bvColor(float bv) {
    static const float k[][4] = { { -0.4f, 150, 175, 255 }, { 0.0f, 200, 215, 255 }, { 0.3f, 235, 235, 255 }, { 0.6f, 255, 244, 232 },
                                  { 1.0f, 255, 214, 165 }, { 1.5f, 255, 170, 112 }, { 2.0f, 255, 135, 85 } };
    const int n = (int)(sizeof(k) / sizeof(k[0]));
    if (bv <= k[0][0]) return { (unsigned char)k[0][1], (unsigned char)k[0][2], (unsigned char)k[0][3] };
    for (int i = 1; i < n; ++i)
        if (bv <= k[i][0]) {
            const float t = (bv - k[i - 1][0]) / (k[i][0] - k[i - 1][0]);
            return { (unsigned char)(k[i - 1][1] + t * (k[i][1] - k[i - 1][1])), (unsigned char)(k[i - 1][2] + t * (k[i][2] - k[i - 1][2])),
                     (unsigned char)(k[i - 1][3] + t * (k[i][3] - k[i - 1][3])) };
        }
    return { (unsigned char)k[n - 1][1], (unsigned char)k[n - 1][2], (unsigned char)k[n - 1][3] };
}

struct GalPoint { float x, y, z; unsigned char r, g, b, a; };

constexpr double GAL_SUN_X = -8178.0, GAL_SUN_Z = 21.0;
constexpr double GAL_ARM_PITCH = 0.2126;
constexpr double GAL_ARM_R0 = 3000.0;
constexpr double GAL_ARM_PHI0 = -2.52;

inline double galArmRadius(int k, double psi) {
    const double phik = GAL_ARM_PHI0 + k * 1.5707963267948966;
    for (int m = -4; m <= 4; ++m) {
        const double r = GAL_ARM_R0 * std::exp(GAL_ARM_PITCH * (psi + 6.283185307179586 * m - phik));
        if (r >= 4000.0 && r <= 13000.0) return r;
    }
    return 8000.0;
}

inline std::vector<GalPoint> generateGalaxy(int n) {
    std::mt19937 rng(20240229);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::normal_distribution<double> N(0.0, 1.0);
    std::vector<GalPoint> pts;
    pts.reserve(n);
    const double TWO_PI = 6.283185307179586;
    auto push = [&](double x, double y, double z, int r, int g, int b, int a) {
        pts.push_back({ (float)x, (float)y, (float)z, (unsigned char)std::clamp(r, 0, 255), (unsigned char)std::clamp(g, 0, 255),
                        (unsigned char)std::clamp(b, 0, 255), (unsigned char)std::clamp(a, 0, 255) });
    };
    const double barAng = 3.141592653589793 - 28.0 * 3.141592653589793 / 180.0;
    for (int i = 0; i < n; ++i) {
        const double u = U(rng);
        if (u < 0.17) {
            const double r = std::fabs(N(rng)) * 900.0 + 60.0, th = U(rng) * TWO_PI, ph = std::acos(2.0 * U(rng) - 1.0);
            push(r * std::sin(ph) * std::cos(th), r * std::sin(ph) * std::sin(th), r * std::cos(ph) * 0.6, 255, 215 + (int)(20 * U(rng)), 150, 120);
        } else if (u < 0.27) {
            const double l = N(rng) * 1900.0, w = N(rng) * 450.0, z = N(rng) * 220.0;
            push(l * std::cos(barAng) - w * std::sin(barAng), l * std::sin(barAng) + w * std::cos(barAng), z, 255, 205, 140, 110);
        } else if (u < 0.69) {
            const int k = (int)(U(rng) * 4.0) & 3;
            double r = 0.0;
            do { r = 2800.0 - 3200.0 * std::log(U(rng) + 1e-9); } while (r > 14500.0);
            const double phik = GAL_ARM_PHI0 + k * 1.5707963267948966;
            const double phi = phik + std::log(r / GAL_ARM_R0) / GAL_ARM_PITCH + N(rng) * (0.10 + 0.05 * (r / 10000.0));
            const double z = N(rng) * (110.0 + 40.0 * (r / 8000.0));
            const bool hii = U(rng) < 0.05;
            if (hii) push(r * std::cos(phi), r * std::sin(phi), z, 255, 120, 170, 200);
            else push(r * std::cos(phi), r * std::sin(phi), z, 170 + (int)(40 * U(rng)), 195 + (int)(30 * U(rng)), 255, 140);
        } else if (u < 0.95) {
            double r = 0.0;
            do { r = -2600.0 * std::log(U(rng) + 1e-9); } while (r > 15500.0);
            const double phi = U(rng) * TWO_PI, z = N(rng) * 260.0;
            push(r * std::cos(phi), r * std::sin(phi), z, 255, 235 + (int)(15 * U(rng)), 200 + (int)(30 * U(rng)), 85);
        } else {
            const double r = std::pow(U(rng), 0.55) * 18000.0, th = U(rng) * TWO_PI, ph = std::acos(2.0 * U(rng) - 1.0);
            push(r * std::sin(ph) * std::cos(th), r * std::sin(ph) * std::sin(th), r * std::cos(ph), 255, 225, 180, 90);
        }
    }
    return pts;
}

inline std::vector<GalPoint> generateMilkyWayBand(int n) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::normal_distribution<double> N(0.0, 1.0);
    std::vector<GalPoint> pts;
    pts.reserve(n);
    const double d2r = 3.141592653589793 / 180.0;
    for (int i = 0; i < n; ++i) {
        double l = U(rng) * 360.0;
        const double bulgeW = std::exp(-std::pow((l > 180.0 ? l - 360.0 : l) / 55.0, 2.0));
        if (U(rng) > 0.35 + 0.65 * bulgeW) { --i; continue; }
        const double b = N(rng) * (5.0 + 5.0 * bulgeW);
        const double gx = std::cos(b * d2r) * std::cos(l * d2r), gy = std::cos(b * d2r) * std::sin(l * d2r), gz = std::sin(b * d2r);
        double x, y, z;
        galToEq(gx, gy, gz, x, y, z);
        const int a = (int)(20 + 55 * bulgeW * U(rng) + 25 * U(rng));
        pts.push_back({ (float)x, (float)y, (float)z, 190, 205, 255, (unsigned char)a });
    }
    return pts;
}
