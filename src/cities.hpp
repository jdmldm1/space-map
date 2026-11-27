#pragma once
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "raylib.h"

struct City {
    std::string name, country;
    double latitude = 0.0, longitude = 0.0, population = 0.0;
    bool capital = false;
    Vector3 surfaceDirection{};
    double importance = 0.0;
};

inline std::vector<City> loadCities(const std::string& path) {
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    constexpr double kCapitalWeight = 3.0;
    std::vector<City> cities;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == ',' || line[i] == '\r') {
                fields.push_back(line.substr(start, i - start));
                start = i + 1;
                if (i < line.size() && line[i] == '\r') break;
            }
        }
        if (fields.size() < 6) continue;
        City city;
        city.name = fields[0];
        city.country = fields[1];
        city.latitude = std::atof(fields[2].c_str());
        city.longitude = std::atof(fields[3].c_str());
        city.population = std::atof(fields[4].c_str());
        city.capital = fields[5] == "1";
        const double lat = city.latitude * kDegreesToRadians, lon = city.longitude * kDegreesToRadians;
        city.surfaceDirection = { (float)(std::cos(lat) * std::cos(lon)), (float)std::sin(lat), (float)(-std::cos(lat) * std::sin(lon)) };
        city.importance = city.population * (city.capital ? kCapitalWeight : 1.0);
        cities.push_back(std::move(city));
    }
    return cities;
}
