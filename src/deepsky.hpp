#pragma once
// Messier deep-sky objects (assets/deepsky.csv, from OpenNGC, CC BY-SA 4.0).
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "orbit.hpp"
#include "paths.hpp"

struct DeepSkyObject {
    std::string name, common, type;
    Vec3d direction{ 1, 0, 0 };  // equatorial J2000 unit vector
    double mag = 99, sizeArcmin = 0;
    std::string label() const { return common.empty() ? name : name + "  " + common; }
};

inline const char* deepSkyTypeName(const std::string& t) {
    if (t == "G") return "Galaxy";
    if (t == "GCl") return "Globular cluster";
    if (t == "OCl" || t == "*Ass") return "Open cluster";
    if (t == "PN") return "Planetary nebula";
    if (t == "SNR") return "Supernova remnant";
    if (t == "Cl+N") return "Cluster with nebula";
    return "Nebula";
}

inline std::vector<DeepSkyObject> loadDeepSky() {
    std::vector<DeepSkyObject> out;
    std::ifstream f(paths::assetPath("assets/deepsky.csv"));
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> c;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == ',' || line[i] == '\r') { c.push_back(line.substr(start, i - start)); start = i + 1; if (i < line.size() && line[i] == '\r') break; }
        if (c.size() < 7) continue;
        DeepSkyObject o;
        o.name = c[0]; o.common = c[1]; o.type = c[2];
        const double ra = std::atof(c[3].c_str()) * DEG, dec = std::atof(c[4].c_str()) * DEG;
        o.direction = { std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec) };
        o.mag = c[5].empty() ? 12.0 : std::atof(c[5].c_str());
        o.sizeArcmin = std::atof(c[6].c_str());
        out.push_back(std::move(o));
    }
    return out;
}
