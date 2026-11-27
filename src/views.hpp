#pragma once
#include <string>

#include "raylib.h"
#include "solar.hpp"

enum class Mode { Globe, Map, Solar, Galaxy, Sky };

struct Cam {
    float yaw = 0.8f, pitch = 0.45f, yawV = 0.0f, pitchV = 0.0f;
    double dist = 1.0, goal = 1.0;
    Vector3 target{ 0, 0, 0 }, tgoal{ 0, 0, 0 };
};

struct Rock { KeplerEl el; double n; double mOff; bool trojan; };

struct GalObj { std::string name; Vector3 pos; Color col; int kind; float sizePc; float mag, bv; };

inline const char* modeName(Mode m) {
    switch (m) { case Mode::Globe: return "globe"; case Mode::Map: return "map"; case Mode::Solar: return "solar system"; case Mode::Sky: return "sky"; default: return "milky way"; }
}
