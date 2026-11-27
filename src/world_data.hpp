#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "galaxy.hpp"
#include "paths.hpp"
#include "raylib.h"
#include "raymath.h"
#include "solar.hpp"
#include "views.hpp"

struct ConstellationSegment { Vec3d from, to; };
struct ConstellationLabel { std::string name; Vec3d direction; };
struct ConstellationFigures {
    std::vector<ConstellationSegment> segments;
    std::vector<ConstellationLabel> labels;
};

inline Vec3d equatorialUnitVector(double rightAscensionDeg, double declinationDeg) {
    const double ra = rightAscensionDeg * DEG, dec = declinationDeg * DEG;
    return { std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec) };
}

inline ConstellationFigures buildConstellationFigures() {
    ConstellationFigures figures;
    for (const Constellation& constellation : loadConstellations(paths::assetPath("assets/constellations.txt"))) {
        for (const auto& segment : constellation.segs)
            figures.segments.push_back({ equatorialUnitVector(segment[0], segment[1]), equatorialUnitVector(segment[2], segment[3]) });
        figures.labels.push_back({ constellation.name, equatorialUnitVector(constellation.labelRa, constellation.labelDec) });
    }
    TraceLog(LOG_INFO, "Constellations: %zu figures, %zu line segments", figures.labels.size(), figures.segments.size());
    return figures;
}

struct SkyStar { Vector3 direction; Color color; int pixelSize; };
struct StarSky {
    std::vector<SkyStar> stars;
    std::vector<std::pair<Vector3, Color>> milkyWayBand;
};

inline StarSky buildStarSky(const std::vector<StarRec>& catalog) {
    constexpr float kFaintestMagnitude = 6.4f;
    StarSky sky;
    for (const StarRec& star : catalog) {
        if (star.mag > kFaintestMagnitude) continue;
        const double ra = star.ra * DEG, dec = star.dec * DEG;
        const Vector3 direction{ (float)(std::cos(dec) * std::cos(ra)), (float)std::sin(dec), (float)-(std::cos(dec) * std::sin(ra)) };
        const float luminance = Clamp(std::pow(10.0f, -0.4f * (star.mag - 1.5f) * 0.5f), 0.22f, 1.0f);
        const Rgb tint = bvColor(star.bv);
        const Color color{ (unsigned char)(tint.r * luminance), (unsigned char)(tint.g * luminance), (unsigned char)(tint.b * luminance), 255 };
        sky.stars.push_back({ direction, color, star.mag < 1.2f ? 3 : star.mag < 2.8f ? 2 : 1 });
    }
    for (const GalPoint& point : generateMilkyWayBand(9000)) sky.milkyWayBand.push_back({ { point.x, point.z, -point.y }, { point.r, point.g, point.b, point.a } });
    return sky;
}

inline Vector3 galacticToRender(double x, double y, double z) { return { (float)x, (float)z, (float)-y }; }

struct SpiralArmName { const char* name; int armIndex; };
inline constexpr SpiralArmName kSpiralArmNames[] = { { "Perseus Arm", 0 }, { "Sagittarius Arm", 1 }, { "Scutum-Centaurus Arm", 2 }, { "Norma Arm", 3 } };

struct GalaxyScene {
    std::vector<GalPoint> points;
    std::vector<Vector3> starPositions;
    std::vector<Color> starColors;
    std::vector<unsigned char> starSizes;
    std::vector<GalObj> objects;
    Vector3 sunPosition{};
    int sunIndex = 0, centreIndex = 0;
};

inline GalaxyScene buildGalaxyScene(const std::vector<StarRec>& catalog) {
    GalaxyScene scene;
    scene.points = generateGalaxy(110000);
    for (const StarRec& star : catalog) {
        if (star.dist <= 0.0f) continue;
        const double ra = star.ra * DEG, dec = star.dec * DEG, distance = star.dist;
        double gx, gy, gz;
        eqToGal(distance * std::cos(dec) * std::cos(ra), distance * std::cos(dec) * std::sin(ra), distance * std::sin(dec), gx, gy, gz);
        const Vector3 position = galacticToRender(GAL_SUN_X + gx, gy, GAL_SUN_Z + gz);
        const double absoluteMagnitude = star.mag - 5.0 * std::log10(distance / 10.0);
        const Rgb tint = bvColor(star.bv);
        const float brightness = Clamp((float)(0.55 + (2.0 - absoluteMagnitude) * 0.12), 0.35f, 1.0f);
        scene.starPositions.push_back(position);
        scene.starColors.push_back({ (unsigned char)(tint.r * brightness), (unsigned char)(tint.g * brightness), (unsigned char)(tint.b * brightness), 255 });
        scene.starSizes.push_back(absoluteMagnitude < -1.0 ? 3 : absoluteMagnitude < 2.0 ? 2 : 1);
        if (!star.name.empty()) scene.objects.push_back({ star.name, position, { tint.r, tint.g, tint.b, 255 }, 0, 0.0f, star.mag, star.bv });
    }
    scene.sunPosition = galacticToRender(GAL_SUN_X, 0, GAL_SUN_Z);
    scene.objects.push_back({ "Sun", scene.sunPosition, { 255, 230, 120, 255 }, 1, 0.0f, -26.7f, 0.65f });
    scene.objects.push_back({ "Sagittarius A* (Galactic Centre)", { 0, 0, 0 }, { 255, 190, 120, 255 }, 1, 0.0f, 0.0f, 0.0f });
    scene.sunIndex = (int)scene.objects.size() - 2;
    scene.centreIndex = scene.sunIndex + 1;
    auto addGalaxy = [&](const char* name, double longitudeDeg, double latitudeDeg, double distanceParsecs, float radiusParsecs, Color color) {
        const double l = longitudeDeg * DEG, b = latitudeDeg * DEG;
        const Vector3 position = galacticToRender(GAL_SUN_X + distanceParsecs * std::cos(b) * std::cos(l), distanceParsecs * std::cos(b) * std::sin(l),
                                                  GAL_SUN_Z + distanceParsecs * std::sin(b));
        scene.objects.push_back({ name, position, color, 2, radiusParsecs, 0.0f, 0.0f });
    };
    addGalaxy("Large Magellanic Cloud", 280.47, -32.89, 49970.0, 5000.0f, { 210, 225, 255, 255 });
    addGalaxy("Small Magellanic Cloud", 302.80, -44.33, 62000.0, 3000.0f, { 210, 225, 255, 255 });
    addGalaxy("Andromeda Galaxy (M31)", 121.17, -21.57, 765000.0, 33000.0f, { 200, 210, 255, 255 });
    addGalaxy("Triangulum Galaxy (M33)", 133.61, -31.33, 840000.0, 15000.0f, { 190, 215, 255, 255 });
    return scene;
}

inline std::vector<Rock> buildSmallBodyField() {
    constexpr double kMeanMotionDegPerDayAtOneAu = 0.9856076686;
    std::mt19937 random(99);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::normal_distribution<double> gaussian(0.0, 1.0);
    std::vector<Rock> rocks;
    for (int i = 0; i < 3500; ++i) {
        double a;
        do { a = 2.75 + 0.38 * gaussian(random); } while (a < 2.1 || a > 3.35);
        rocks.push_back({ { a, std::min(0.28, std::fabs(gaussian(random)) * 0.09), std::fabs(gaussian(random)) * 7.0, uniform(random) * 360.0, uniform(random) * 360.0, uniform(random) * 360.0 },
                          kMeanMotionDegPerDayAtOneAu / std::pow(a, 1.5), 0.0, false });
    }
    for (int i = 0; i < 700; ++i) {
        const double a = 5.2 + 0.12 * gaussian(random);
        const double offsetFromJupiterDeg = (i & 1 ? 60.0 : -60.0) + 9.0 * gaussian(random);
        rocks.push_back({ { a, std::fabs(gaussian(random)) * 0.06, std::fabs(gaussian(random)) * 12.0, uniform(random) * 360.0, uniform(random) * 360.0, 0.0 }, 0.0, offsetFromJupiterDeg, true });
    }
    for (int i = 0; i < 3000; ++i) {
        const double a = 39.4 + 8.6 * uniform(random);
        rocks.push_back({ { a, std::fabs(gaussian(random)) * 0.07, std::fabs(gaussian(random)) * 9.0, uniform(random) * 360.0, uniform(random) * 360.0, uniform(random) * 360.0 },
                          kMeanMotionDegPerDayAtOneAu / std::pow(a, 1.5), 0.0, false });
    }
    return rocks;
}
