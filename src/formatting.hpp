#pragma once
#include <cctype>
#include <string>

#include "raylib.h"
#include "time_utils.hpp"

inline std::string fmtClock(double unixSeconds, bool local) {
    return formatTime(unixSeconds, local ? "%Y-%m-%d %H:%M:%S local" : "%Y-%m-%d %H:%M:%S UTC", local);
}

inline std::string fmtHm(double unixSeconds) { return formatTime(unixSeconds, "%H:%M"); }

inline std::string lowerStr(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

inline std::string fmtParsec(double parsecs) {
    const double lightYears = parsecs * 3.26156;
    if (parsecs >= 1000.0) return TextFormat("%.2f kpc  (%.0f kly)", parsecs / 1000.0, lightYears / 1000.0);
    if (parsecs >= 10.0) return TextFormat("%.0f pc  (%.0f ly)", parsecs, lightYears);
    return TextFormat("%.2f pc  (%.1f ly)", parsecs, lightYears);
}
