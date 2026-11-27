#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "paths.hpp"
#include "raylib.h"

inline Font g_font{};
inline bool g_fontOk = false;

inline void uiLoadFont() {
    const char* cands[] = { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/System/Library/Fonts/Helvetica.ttc" };
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c : { 0xB0, 0xB1, 0xB7, 0xD7, 0xA9, 0x2013, 0x2014, 0x2212, 0x2191, 0x2193, 0x2022, 0x3BC }) cps.push_back(c);
    for (const char* p : cands) {
        if (!FileExists(p)) continue;
        g_font = LoadFontEx(p, 40, cps.data(), (int)cps.size());
        if (g_font.texture.id) { SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR); g_fontOk = true; return; }
    }
}

inline void UiText(const char* t, int x, int y, int size, Color c) {
    if (g_fontOk) DrawTextEx(g_font, t, { (float)x, (float)y - size * 0.10f }, (float)size * 1.04f, 0.0f, c);
    else ::DrawText(t, x, y, size, c);
}
inline int UiMeasure(const char* t, int size) {
    if (g_fontOk) return (int)MeasureTextEx(g_font, t, (float)size * 1.04f, 0.0f).x;
    return ::MeasureText(t, size);
}

struct Options {
    int units = 0;
    bool localTime = false;
    float markerScale = 1.0f, starBright = 1.0f;
    bool showClouds = true, showSpecular = true, showNightLights = true, showConst = true, showCities = true;
    bool satLEO = true, satMEO = true, satGEO = true, satHEO = true;
    int satCategory = 0;
    bool acGround = true, acTrails = true; int acMinAlt = 0;
    bool shipCargo = true, shipTanker = true, shipPass = true, shipFish = true, shipOther = true, shipMoving = false;
    int baseMap = 1;
    float radarOpacity = 0.75f;
    bool radarOn = false, cloudsOn = false, windOn = false;
    bool showHints = true, vsync = true, showStatus = true;
    bool showMissions = true, showDeepSky = true;
    bool satCat[6] = { true, true, true, true, true, true };  // satellite group filters
    int passHours = 0, passMinEl = 0, alertLead = 1;       // planner horizon (24h/3d/7d), min elevation (0/10/20/30), alert lead (5/10/30 min)
    bool passAlerts = false, osNotify = true;
    int missionTrails = 0;  // 0 = short tails around each spacecraft, 1 = full paths, 2 = selected only
    std::string aisNmea;
    std::string radarNote;

    void reset() { *this = Options(); }
    static std::string path() { return paths::dataPath("spacemap.cfg"); }
    void load() {
        std::ifstream f(path());
        std::string line;
        std::map<std::string, std::string> kv;
        while (std::getline(f, line)) {
            const size_t e = line.find('=');
            if (e == std::string::npos || line[0] == '#') continue;
            std::string v = line.substr(e + 1);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            kv[line.substr(0, e)] = v;
        }
        auto I = [&](const char* k, int& v) { if (kv.count(k)) v = std::atoi(kv[k].c_str()); };
        auto B = [&](const char* k, bool& v) { if (kv.count(k)) v = std::atoi(kv[k].c_str()) != 0; };
        auto F = [&](const char* k, float& v) { if (kv.count(k)) v = (float)std::atof(kv[k].c_str()); };
        I("units", units); B("localTime", localTime); F("markerScale", markerScale); F("starBright", starBright); B("showClouds", showClouds); B("showNightLights", showNightLights); B("showConst", showConst); B("showCities", showCities); B("showSpecular", showSpecular);
        B("satLEO", satLEO); B("satMEO", satMEO); B("satGEO", satGEO); B("satHEO", satHEO); I("satCategory", satCategory); B("acGround", acGround); B("acTrails", acTrails); I("acMinAlt", acMinAlt);
        B("shipCargo", shipCargo); B("shipTanker", shipTanker); B("shipPass", shipPass); B("shipFish", shipFish); B("shipOther", shipOther); B("shipMoving", shipMoving);
        I("baseMap", baseMap); F("radarOpacity", radarOpacity); B("showHints", showHints); B("vsync", vsync); B("showStatus", showStatus); B("showMissions", showMissions); B("showDeepSky", showDeepSky); I("missionTrails", missionTrails); I("passHours", passHours); I("passMinEl", passMinEl); I("alertLead", alertLead); B("passAlerts", passAlerts); B("osNotify", osNotify);
        if (kv.count("satCats")) { const std::string& sc = kv["satCats"]; for (int i = 0; i < 6 && i < (int)sc.size(); ++i) satCat[i] = sc[i] != '0'; }
        if (kv.count("ais_nmea")) aisNmea = kv["ais_nmea"];
        missionTrails = std::clamp(missionTrails, 0, 2); passHours = std::clamp(passHours, 0, 2); passMinEl = std::clamp(passMinEl, 0, 3); alertLead = std::clamp(alertLead, 0, 2); units = std::clamp(units, 0, 2); baseMap = std::clamp(baseMap, 0, 3); acMinAlt = std::clamp(acMinAlt, 0, 3); satCategory = std::clamp(satCategory, 0, 6);
        markerScale = std::clamp(markerScale, 0.5f, 2.5f); starBright = std::clamp(starBright, 0.4f, 2.0f); radarOpacity = std::clamp(radarOpacity, 0.2f, 1.0f);
    }
    void save() const {
        std::ofstream f(path());
        f << "# spacemap options (edited from the Settings panel). ais_nmea=host:port adds a raw NMEA AIS feed, e.g. a local receiver.\n";
        f << "units=" << units << "\nlocalTime=" << localTime << "\nmarkerScale=" << markerScale << "\nstarBright=" << starBright << "\nshowClouds=" << showClouds << "\nshowNightLights=" << showNightLights << "\nshowConst=" << showConst << "\nshowCities=" << showCities
          << "\nshowSpecular=" << showSpecular << "\nsatLEO=" << satLEO << "\nsatMEO=" << satMEO << "\nsatGEO=" << satGEO << "\nsatHEO=" << satHEO << "\nsatCategory=" << satCategory
          << "\nacGround=" << acGround << "\nacTrails=" << acTrails << "\nacMinAlt=" << acMinAlt << "\nshipCargo=" << shipCargo << "\nshipTanker=" << shipTanker
          << "\nshipPass=" << shipPass << "\nshipFish=" << shipFish << "\nshipOther=" << shipOther << "\nshipMoving=" << shipMoving << "\nbaseMap=" << baseMap
          << "\nradarOpacity=" << radarOpacity << "\nshowHints=" << showHints << "\nvsync=" << vsync << "\nshowStatus=" << showStatus << "\nshowMissions=" << showMissions << "\nshowDeepSky=" << showDeepSky << "\nmissionTrails=" << missionTrails << "\npassHours=" << passHours << "\npassMinEl=" << passMinEl << "\nalertLead=" << alertLead << "\npassAlerts=" << passAlerts << "\nosNotify=" << osNotify << "\nsatCats=" << (satCat[0] ? 1 : 0) << (satCat[1] ? 1 : 0) << (satCat[2] ? 1 : 0) << (satCat[3] ? 1 : 0) << (satCat[4] ? 1 : 0) << (satCat[5] ? 1 : 0) << "\nais_nmea=" << aisNmea << "\n";
    }
};

inline std::string fmtLen(const Options& o, double meters) {
    char b[48];
    if (o.units == 0) std::snprintf(b, sizeof b, "%.0f m", meters); else std::snprintf(b, sizeof b, "%.0f ft", meters * 3.28084);
    return b;
}
inline std::string fmtAltFt(const Options& o, double ft) {
    char b[48];
    if (o.units == 0) std::snprintf(b, sizeof b, "%.0f m  (FL%.0f)", ft * 0.3048, ft / 100.0); else std::snprintf(b, sizeof b, "%.0f ft  (FL%.0f)", ft, ft / 100.0);
    return b;
}
inline std::string fmtSpeedKt(const Options& o, double kt) {
    char b[48];
    if (o.units == 0) std::snprintf(b, sizeof b, "%.0f km/h  (%.0f kt)", kt * 1.852, kt);
    else if (o.units == 1) std::snprintf(b, sizeof b, "%.0f mph  (%.0f kt)", kt * 1.15078, kt);
    else std::snprintf(b, sizeof b, "%.0f kt", kt);
    return b;
}
inline std::string fmtDistKm(const Options& o, double km) {
    char b[48];
    if (o.units == 0) std::snprintf(b, sizeof b, km >= 100 ? "%.0f km" : "%.1f km", km);
    else if (o.units == 1) std::snprintf(b, sizeof b, km * 0.621371 >= 100 ? "%.0f mi" : "%.1f mi", km * 0.621371);
    else std::snprintf(b, sizeof b, km / 1.852 >= 100 ? "%.0f nm" : "%.1f nm", km / 1.852);
    return b;
}
inline std::string fmtTemp(const Options& o, double c) {
    char b[32];
    if (o.units == 1) std::snprintf(b, sizeof b, "%.0f\xC2\xB0""F", c * 9.0 / 5.0 + 32.0); else std::snprintf(b, sizeof b, "%.1f\xC2\xB0""C", c);
    return b;
}

struct Row { std::string k, v; Color c = { 235, 238, 245, 255 }; bool sep = false; bool wide = false; };
struct PanelData { std::string title, sub; Color tc = WHITE; std::vector<Row> rows; };

inline void uiShadowBox(Rectangle r, float alpha = 0.78f) {
    DrawRectangleRounded({ r.x + 2, r.y + 4, r.width, r.height }, 0.04f, 6, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.04f, 6, { 12, 15, 24, (unsigned char)(255 * alpha) });
    DrawRectangleRoundedLines(r, 0.04f, 6, Fade(WHITE, 0.10f));
}

inline int uiDrawPanel(const PanelData& p, int x, int y, int w) {
    int h = 58;
    for (const Row& r : p.rows) h += r.sep ? 26 : r.wide ? 18 : 20;
    h += 6;
    uiShadowBox({ (float)x, (float)y, (float)w, (float)h });
    DrawRectangle(x + 1, y + 1, 4, 52, Fade(p.tc, 0.9f));
    UiText(p.title.c_str(), x + 14, y + 8, UiMeasure(p.title.c_str(), 20) > w - 24 ? 16 : 20, p.tc);
    UiText(p.sub.c_str(), x + 14, y + 33, 13, { 150, 158, 175, 255 });
    int yy = y + 56;
    for (const Row& r : p.rows) {
        if (r.sep) {
            DrawLine(x + 12, yy + 14, x + w - 12, yy + 14, Fade(WHITE, 0.10f));
            UiText(r.k.c_str(), x + 14, yy + 2, 11, { 120, 130, 150, 255 });
            yy += 26;
        } else if (r.wide) {
            UiText(r.v.c_str(), x + 14, yy, 13, r.c);
            yy += 18;
        } else {
            UiText(r.k.c_str(), x + 14, yy, 13, { 140, 150, 170, 255 });
            UiText(r.v.c_str(), x + 108, yy - 1, 14, r.c);
            yy += 20;
        }
    }
    return h;
}

struct Ui {
    std::vector<Rectangle> prev, cur;
    Vector2 mouse{ 0, 0 };
    bool overUi = false, pressedOnUi = false, released = false;
    std::string tip;
    Vector2 tipAt{ 0, 0 };

    void begin() {
        prev.swap(cur); cur.clear();
        mouse = GetMousePosition();
        overUi = false;
        for (const Rectangle& r : prev) if (CheckCollisionPointRec(mouse, r)) { overUi = true; break; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) pressedOnUi = overUi;
        released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        tip.clear();
    }
    void end() { if (released || !(IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT))) { if (released) pressedOnUi = false; } }
    void region(Rectangle r) { cur.push_back(r); }
    bool hover(Rectangle r) const { return CheckCollisionPointRec(mouse, r); }
    bool clicked(Rectangle r) const { return released && pressedOnUi && hover(r); }
    bool captured() const { return overUi || pressedOnUi; }

    bool button(Rectangle r, const char* label, bool active = false, const char* tooltip = nullptr, Color accent = { 90, 160, 255, 255 }) {
        region(r);
        const bool h = hover(r);
        const Color bg = active ? Fade(accent, h ? 0.55f : 0.40f) : (h ? Color{ 60, 70, 95, 235 } : Color{ 30, 36, 52, 215 });
        DrawRectangleRounded(r, 0.28f, 6, bg);
        if (active) DrawRectangleRoundedLines(r, 0.28f, 6, Fade(accent, 0.8f));
        const int fs = 13, tw = UiMeasure(label, fs);
        UiText(label, (int)(r.x + (r.width - tw) * 0.5f), (int)(r.y + (r.height - fs) * 0.5f - 1), fs, active ? WHITE : Color{ 205, 212, 228, 255 });
        if (h && tooltip) { tip = tooltip; tipAt = mouse; }
        return clicked(r);
    }

    bool toggle(float x, float y, float w, const char* label, bool& v, const char* tooltip = nullptr) {
        const Rectangle r{ x, y, w, 22 };
        region(r);
        const bool h = hover(r);
        if (h) DrawRectangleRounded(r, 0.2f, 4, Fade(WHITE, 0.06f));
        const Rectangle box{ x + 4, y + 4, 14, 14 };
        DrawRectangleRounded(box, 0.25f, 4, v ? Color{ 80, 150, 255, 255 } : Color{ 38, 44, 62, 255 });
        DrawRectangleRoundedLines(box, 0.25f, 4, Fade(WHITE, 0.3f));
        if (v) { DrawLineEx({ box.x + 3, box.y + 7 }, { box.x + 6, box.y + 10.5f }, 2, WHITE); DrawLineEx({ box.x + 6, box.y + 10.5f }, { box.x + 11.5f, box.y + 3.5f }, 2, WHITE); }
        UiText(label, (int)x + 26, (int)y + 3, 14, { 220, 226, 238, 255 });
        if (h && tooltip) { tip = tooltip; tipAt = mouse; }
        if (clicked(r)) { v = !v; return true; }
        return false;
    }

    bool stepper(float x, float y, float w, const char* label, const std::vector<std::string>& names, int& idx) {
        const Rectangle r{ x, y, w, 24 };
        region(r);
        UiText(label, (int)x + 4, (int)y + 4, 14, { 170, 180, 200, 255 });
        const float bw = 22, vx = x + w - 150;
        const Rectangle lb{ vx, y + 1, bw, 22 }, rb{ x + w - bw, y + 1, bw, 22 };
        bool ch = false;
        if (button(lb, "<")) { idx = (idx + (int)names.size() - 1) % (int)names.size(); ch = true; }
        if (button(rb, ">")) { idx = (idx + 1) % (int)names.size(); ch = true; }
        const std::string& s = names[idx];
        UiText(s.c_str(), (int)(vx + bw + (150 - 2 * bw - UiMeasure(s.c_str(), 14)) * 0.5f), (int)y + 4, 14, WHITE);
        return ch;
    }
    bool slider(float x, float y, float w, const char* label, float& v, float lo, float hi, const char* fmt = "%.2f") {
        const Rectangle r{ x, y, w, 24 };
        region(r);
        UiText(label, (int)x + 4, (int)y + 4, 14, { 170, 180, 200, 255 });
        const Rectangle track{ x + w - 150, y + 10, 110, 4 };
        const Rectangle hit{ track.x - 6, y, track.width + 12, 24 };
        bool ch = false;
        if (pressedOnUi && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hover(hit)) { v = lo + std::clamp((mouse.x - track.x) / track.width, 0.0f, 1.0f) * (hi - lo); ch = true; }
        DrawRectangleRounded(track, 1.0f, 4, { 50, 58, 80, 255 });
        const float t = (v - lo) / (hi - lo);
        DrawRectangleRounded({ track.x, track.y, track.width * t, track.height }, 1.0f, 4, { 80, 150, 255, 255 });
        DrawCircleV({ track.x + track.width * t, track.y + 2 }, 6, WHITE);
        char b[24]; std::snprintf(b, sizeof b, fmt, v);
        UiText(b, (int)(track.x + track.width + 10), (int)y + 4, 12, { 200, 208, 225, 255 });
        return ch;
    }
    void drawTooltip() {
        if (tip.empty()) return;
        const int w = UiMeasure(tip.c_str(), 13) + 16;
        float x = tipAt.x + 14, y = tipAt.y + 20;
        x = std::min(x, (float)GetScreenWidth() - w - 6); y = std::min(y, (float)GetScreenHeight() - 30);
        DrawRectangleRounded({ x, y, (float)w, 24 }, 0.25f, 6, { 10, 12, 20, 240 });
        DrawRectangleRoundedLines({ x, y, (float)w, 24 }, 0.25f, 6, Fade(WHITE, 0.2f));
        UiText(tip.c_str(), (int)x + 8, (int)y + 5, 13, WHITE);
    }
};
