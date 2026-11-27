#pragma once
// Vector silhouettes rasterised into a single sprite atlas at start-up (no external image files).
// Every icon is drawn pointing up (nose / bow towards the top) in white, with soft-edged alpha, and is tinted when drawn.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"

enum class Icon {
    Airliner, Regional, BizJet, LightPlane, Helicopter, Fighter,
    ShipCargo, ShipTanker, ShipPassenger, ShipFishing, ShipSailing, ShipTug,
    SatGeneric, SatStarlink, SatStation, SatTelescope, SatNav, SatWeather, Capsule, Probe, Rocket,
    Count
};

namespace icons_detail {
struct Poly { std::vector<Vector2> pts; unsigned char alpha; };
using Shape = std::vector<Poly>;

inline Poly P(std::initializer_list<Vector2> pts, unsigned char a = 255) { return { std::vector<Vector2>(pts), a }; }
inline Poly mirrored(const Poly& p) { Poly m = p; for (Vector2& v : m.pts) v.x = -v.x; std::reverse(m.pts.begin(), m.pts.end()); return m; }
inline void both(Shape& s, const Poly& p) { s.push_back(p); s.push_back(mirrored(p)); }
inline Poly rect(float x0, float y0, float x1, float y1, unsigned char a = 255) { return P({ { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }, a); }
inline Poly bar(float cx, float cy, float len, float width, float deg, unsigned char a = 255) {
    const float r = deg * 3.14159265f / 180.0f, dx = std::cos(r) * len * 0.5f, dy = std::sin(r) * len * 0.5f, nx = -std::sin(r) * width * 0.5f, ny = std::cos(r) * width * 0.5f;
    return P({ { cx - dx - nx, cy - dy - ny }, { cx + dx - nx, cy + dy - ny }, { cx + dx + nx, cy + dy + ny }, { cx - dx + nx, cy - dy + ny } }, a);
}
inline Poly ellipse(float cx, float cy, float rx, float ry, unsigned char a = 255) {
    Poly p; p.alpha = a;
    for (int i = 0; i < 20; ++i) { const float t = i * 6.2831853f / 20.0f; p.pts.push_back({ cx + std::cos(t) * rx, cy + std::sin(t) * ry }); }
    return p;
}

inline Shape shapeFor(Icon icon) {
    Shape s;
    auto fuselage = [&](float halfW, float tailY) { s.push_back(P({ { 0, -1 }, { halfW * 0.8f, -0.86f }, { halfW, -0.35f }, { halfW, tailY - 0.2f }, { halfW * 0.45f, tailY }, { 0, tailY + 0.05f },
                                                                 { -halfW * 0.45f, tailY }, { -halfW, tailY - 0.2f }, { -halfW, -0.35f }, { -halfW * 0.8f, -0.86f } })); };
    switch (icon) {
        case Icon::Airliner:
            fuselage(0.085f, 0.92f);
            both(s, P({ { 0.08f, -0.28f }, { 0.97f, 0.38f }, { 0.97f, 0.53f }, { 0.08f, 0.12f } }));
            both(s, P({ { 0.05f, 0.7f }, { 0.4f, 0.96f }, { 0.4f, 1.0f }, { 0.04f, 0.9f } }));
            both(s, rect(0.32f, 0.05f, 0.4f, 0.3f, 200));  // engines
            break;
        case Icon::Regional:
            fuselage(0.075f, 0.9f);
            both(s, P({ { 0.07f, -0.12f }, { 0.95f, -0.12f }, { 0.95f, 0.12f }, { 0.07f, 0.16f } }));
            both(s, P({ { 0.05f, 0.72f }, { 0.4f, 0.8f }, { 0.4f, 0.95f }, { 0.04f, 0.93f } }));
            both(s, rect(0.3f, -0.34f, 0.38f, -0.05f, 210));  // propeller discs
            break;
        case Icon::BizJet:
            fuselage(0.065f, 0.88f);
            both(s, P({ { 0.06f, -0.02f }, { 0.75f, 0.52f }, { 0.75f, 0.66f }, { 0.06f, 0.34f } }));
            both(s, P({ { 0.04f, 0.74f }, { 0.34f, 0.97f }, { 0.34f, 1.0f }, { 0.04f, 0.92f } }));
            both(s, rect(0.1f, 0.5f, 0.2f, 0.85f, 200));  // rear engines
            break;
        case Icon::LightPlane:
            s.push_back(P({ { 0, -0.85f }, { 0.07f, -0.7f }, { 0.08f, 0.5f }, { 0.03f, 0.88f }, { -0.03f, 0.88f }, { -0.08f, 0.5f }, { -0.07f, -0.7f } }));
            both(s, P({ { 0.07f, -0.38f }, { 1.0f, -0.38f }, { 1.0f, -0.04f }, { 0.07f, -0.02f } }));
            both(s, P({ { 0.04f, 0.72f }, { 0.42f, 0.74f }, { 0.42f, 0.92f }, { 0.04f, 0.9f } }));
            s.push_back(bar(0, -0.88f, 0.9f, 0.05f, 0, 180));  // propeller
            break;
        case Icon::Helicopter:
            s.push_back(ellipse(0, -0.08f, 0.26f, 0.5f));
            s.push_back(rect(-0.05f, 0.3f, 0.05f, 0.95f));
            s.push_back(rect(-0.22f, 0.86f, 0.22f, 0.95f));
            s.push_back(bar(0, -0.1f, 2.0f, 0.07f, 35, 190));
            s.push_back(bar(0, -0.1f, 2.0f, 0.07f, -55, 190));
            break;
        case Icon::Fighter:
            s.push_back(P({ { 0, -1 }, { 0.08f, -0.55f }, { 0.12f, 0.4f }, { 0.07f, 0.9f }, { 0, 0.82f }, { -0.07f, 0.9f }, { -0.12f, 0.4f }, { -0.08f, -0.55f } }));
            both(s, P({ { 0.1f, -0.25f }, { 0.95f, 0.72f }, { 0.95f, 0.88f }, { 0.1f, 0.58f } }));
            both(s, P({ { 0.08f, 0.62f }, { 0.35f, 0.92f }, { 0.35f, 1.0f }, { 0.06f, 0.9f } }));
            break;
        case Icon::ShipCargo:
            s.push_back(P({ { 0, -1 }, { 0.2f, -0.72f }, { 0.24f, 0.92f }, { -0.24f, 0.92f }, { -0.2f, -0.72f } }));
            s.push_back(rect(-0.17f, -0.55f, 0.17f, -0.3f, 150)); s.push_back(rect(-0.17f, -0.22f, 0.17f, 0.03f, 150)); s.push_back(rect(-0.17f, 0.11f, 0.17f, 0.36f, 150));
            s.push_back(rect(-0.2f, 0.5f, 0.2f, 0.9f, 255));
            break;
        case Icon::ShipTanker:
            s.push_back(P({ { 0, -1 }, { 0.24f, -0.78f }, { 0.27f, 0.9f }, { 0.2f, 0.95f }, { -0.2f, 0.95f }, { -0.27f, 0.9f }, { -0.24f, -0.78f } }));
            s.push_back(rect(-0.03f, -0.7f, 0.03f, 0.4f, 120)); s.push_back(rect(-0.2f, 0.55f, 0.2f, 0.92f, 255));
            break;
        case Icon::ShipPassenger:
            s.push_back(P({ { 0, -1 }, { 0.22f, -0.7f }, { 0.25f, 0.92f }, { -0.25f, 0.92f }, { -0.22f, -0.7f } }));
            s.push_back(rect(-0.2f, -0.5f, 0.2f, 0.7f, 130)); s.push_back(rect(-0.1f, 0.15f, 0.1f, 0.4f, 255));
            break;
        case Icon::ShipFishing:
            s.push_back(P({ { 0, -0.95f }, { 0.28f, -0.5f }, { 0.3f, 0.85f }, { -0.3f, 0.85f }, { -0.28f, -0.5f } }));
            s.push_back(rect(-0.14f, 0.1f, 0.14f, 0.7f, 140)); s.push_back(bar(0, -0.15f, 0.9f, 0.05f, 90, 200));
            break;
        case Icon::ShipSailing:
            s.push_back(P({ { 0, -1 }, { 0.12f, -0.4f }, { 0.12f, 0.85f }, { -0.12f, 0.85f }, { -0.12f, -0.4f } }));
            s.push_back(P({ { 0.0f, -0.6f }, { 0.55f, 0.35f }, { 0.0f, 0.25f } }, 170));
            s.push_back(P({ { -0.02f, -0.45f }, { -0.4f, 0.2f }, { -0.02f, 0.12f } }, 120));
            break;
        case Icon::ShipTug:
            s.push_back(P({ { 0, -0.8f }, { 0.3f, -0.4f }, { 0.34f, 0.8f }, { -0.34f, 0.8f }, { -0.3f, -0.4f } }));
            s.push_back(rect(-0.18f, -0.1f, 0.18f, 0.35f, 140));
            break;
        case Icon::SatGeneric:
            s.push_back(rect(-0.2f, -0.2f, 0.2f, 0.2f));
            both(s, rect(0.28f, -0.17f, 0.97f, 0.17f, 170)); both(s, rect(0.2f, -0.03f, 0.28f, 0.03f));
            s.push_back(ellipse(0, -0.33f, 0.1f, 0.1f));
            break;
        case Icon::SatStarlink:
            s.push_back(rect(-0.16f, -0.16f, 0.16f, 0.16f));
            s.push_back(rect(0.16f, -0.55f, 0.95f, 0.55f, 170)); s.push_back(rect(-0.5f, -0.05f, -0.16f, 0.05f, 200)); s.push_back(ellipse(-0.55f, 0, 0.12f, 0.12f));
            break;
        case Icon::SatStation:
            s.push_back(rect(-1.0f, -0.05f, 1.0f, 0.05f)); s.push_back(rect(-0.1f, -0.3f, 0.1f, 0.3f));
            for (float x : { -0.8f, -0.55f, 0.55f, 0.8f }) s.push_back(rect(x - 0.1f, -0.7f, x + 0.1f, 0.7f, 170));
            s.push_back(rect(-0.05f, -0.45f, 0.05f, 0.45f));
            break;
        case Icon::SatTelescope:
            s.push_back(rect(-0.17f, -0.7f, 0.17f, 0.55f)); s.push_back(ellipse(0, -0.72f, 0.24f, 0.1f, 200));
            both(s, rect(0.2f, -0.1f, 0.85f, 0.35f, 170));
            break;
        case Icon::SatNav:
            s.push_back(ellipse(0, 0, 0.26f, 0.26f));
            both(s, rect(0.3f, -0.12f, 1.0f, 0.12f, 170)); both(s, rect(0.26f, -0.03f, 0.3f, 0.03f));
            s.push_back(rect(-0.05f, -0.5f, 0.05f, -0.26f));
            break;
        case Icon::SatWeather:
            s.push_back(rect(-0.2f, -0.25f, 0.2f, 0.25f));
            s.push_back(rect(0.2f, -0.18f, 0.95f, 0.18f, 170)); s.push_back(ellipse(-0.2f, -0.2f, 0.22f, 0.22f, 220));
            break;
        case Icon::Capsule:
            s.push_back(P({ { 0, -0.95f }, { 0.28f, -0.2f }, { -0.28f, -0.2f } })); s.push_back(rect(-0.2f, -0.2f, 0.2f, 0.5f, 220)); s.push_back(P({ { -0.12f, 0.5f }, { 0.12f, 0.5f }, { 0.2f, 0.75f }, { -0.2f, 0.75f } }));
            both(s, rect(0.22f, 0.0f, 0.9f, 0.3f, 170));
            break;
        case Icon::Probe:
            s.push_back(ellipse(0, -0.35f, 0.42f, 0.2f)); s.push_back(rect(-0.18f, -0.3f, 0.18f, 0.35f, 230)); s.push_back(rect(-0.03f, 0.35f, 0.03f, 1.0f, 200));
            both(s, rect(0.2f, -0.05f, 0.7f, 0.25f, 170));
            break;
        case Icon::Rocket:
            s.push_back(P({ { 0, -1 }, { 0.13f, -0.6f }, { 0.13f, 0.75f }, { -0.13f, 0.75f }, { -0.13f, -0.6f } }));
            both(s, P({ { 0.13f, 0.35f }, { 0.38f, 0.9f }, { 0.13f, 0.75f } })); s.push_back(P({ { -0.08f, 0.75f }, { 0.08f, 0.75f }, { 0.0f, 1.0f } }, 170));
            break;
        default: break;
    }
    return s;
}

inline bool inside(const std::vector<Vector2>& pts, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
        if (((pts[i].y > y) != (pts[j].y > y)) && (x < (pts[j].x - pts[i].x) * (y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x)) in = !in;
    return in;
}
}  // namespace icons_detail

struct IconAtlas {
    Texture2D tex{};
    int cell = 64, cols = 8;
    bool ok = false;

    void build() {
        using namespace icons_detail;
        const int n = (int)Icon::Count, rows = (n + cols - 1) / cols, W = cols * cell, H = rows * cell, SS = 3;
        std::vector<unsigned char> px((size_t)W * H * 4, 0);
        for (int ic = 0; ic < n; ++ic) {
            const Shape shape = shapeFor((Icon)ic);
            const int ox = (ic % cols) * cell, oy = (ic / cols) * cell;
            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x) {
                    float sum = 0.0f;
                    for (int sy = 0; sy < SS; ++sy)
                        for (int sx = 0; sx < SS; ++sx) {
                            const float fx = ((x + (sx + 0.5f) / SS) / cell - 0.5f) * 2.0f / 0.92f, fy = ((y + (sy + 0.5f) / SS) / cell - 0.5f) * 2.0f / 0.92f;
                            float a = 0.0f;
                            for (const Poly& p : shape) if (inside(p.pts, fx, fy)) a = p.alpha / 255.0f;  // later polygons draw over earlier ones
                            sum += a;
                        }
                    const unsigned char alpha = (unsigned char)std::min(255.0f, sum / (SS * SS) * 255.0f);
                    unsigned char* d = &px[((size_t)(oy + y) * W + ox + x) * 4];
                    d[0] = d[1] = d[2] = 255; d[3] = alpha;
                }
        }
        Image img{ px.data(), W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        tex = LoadTextureFromImage(img);
        if (tex.id) { GenTextureMipmaps(&tex); SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR); ok = true; }
    }

    // Draws an icon centred on (x, y), `size` pixels across, rotated clockwise by `rotationDeg` (icons point up).
    void draw(Icon icon, Vector2 at, float size, float rotationDeg, Color tint) const {
        if (!ok) return;
        const int i = (int)icon;
        const Rectangle src{ (float)((i % cols) * cell), (float)((i / cols) * cell), (float)cell, (float)cell };
        DrawTexturePro(tex, src, { at.x, at.y, size, size }, { size * 0.5f, size * 0.5f }, rotationDeg, tint);
    }
    // Dark halo underneath keeps light icons readable on bright map tiles.
    void drawOutlined(Icon icon, Vector2 at, float size, float rotationDeg, Color tint) const {
        draw(icon, at, size * 1.28f, rotationDeg, Fade(BLACK, 0.6f));
        draw(icon, at, size, rotationDeg, tint);
    }
};

// ---- Choosing an icon from catalogue data --------------------------------------------------------------------------
inline Icon aircraftIcon(const std::string& type, float* sizeScale = nullptr) {
    static std::unordered_map<std::string, std::pair<Icon, float>> cache;
    auto it = cache.find(type);
    if (it == cache.end()) {
        auto in = [&](std::initializer_list<const char*> list) { for (const char* s : list) if (type == s) return true; return false; };
        auto starts = [&](const char* p) { return type.rfind(p, 0) == 0; };
        std::pair<Icon, float> r{ Icon::Airliner, 1.0f };
        if (in({ "R44", "R22", "R66", "EC35", "EC30", "EC20", "EC45", "EC55", "A139", "A109", "A119", "S76", "S92", "S61", "B06", "B407", "B412", "B429", "B505", "H60", "H47", "H64", "AS50", "AS55", "AS65", "MD52", "MD60", "H500", "EN28", "EN48", "B212", "B222", "S58", "H145" })) r = { Icon::Helicopter, 1.0f };
        else if (starts("F16") || starts("F15") || starts("F18") || starts("F22") || starts("F35") || in({ "T38", "A10", "EUFI", "RFAL", "TORN", "GRIP", "F5", "T6", "FA18", "HAWK", "M346" })) r = { Icon::Fighter, 0.9f };
        else if (in({ "C150", "C152", "C172", "C177", "C182", "C185", "C206", "C210", "C170", "C140", "PA28", "PA32", "PA34", "PA44", "PA46", "PA18", "PA38", "P28A", "P28B", "SR20", "SR22", "DA40", "DA42", "DA62", "M20P", "M20T", "BE33", "BE35", "BE36", "BE55", "BE58", "AA5", "G115", "RV6", "RV7", "RV8", "RV10", "COZY", "J3", "T206", "T210", "SIRA", "TOBA", "BL8", "AC11", "P180", "GLID", "ULAC", "BALL", "PC6T" })) r = { Icon::LightPlane, 0.85f };
        else if (in({ "AT72", "AT76", "AT75", "AT45", "AT46", "DH8A", "DH8B", "DH8C", "DH8D", "SF34", "SB20", "E120", "BE20", "BE9L", "BE10", "B190", "PC12", "C208", "TBM7", "TBM8", "TBM9", "JS32", "JS41", "D228", "DHC6", "DHC2", "L410", "SW4", "SH36", "C130", "C30J", "A400", "CN35", "C295", "P3", "E2", "E2C" })) r = { Icon::Regional, 1.0f };
        else if (starts("C25") || starts("C51") || starts("C52") || starts("C55") || starts("C56") || starts("C68") || starts("C70") || starts("C75") || starts("GLF") || starts("GL") || starts("CL") || starts("LJ") || starts("FA") || starts("E50") || starts("E55") ||
                 in({ "F900", "F2TH", "H25B", "H25C", "PC24", "HDJT", "BE40", "PRM1", "SF50", "EA50", "LJ45", "G280", "FA7X", "FA8X" })) r = { Icon::BizJet, 0.95f };
        else if (starts("A38") || starts("B74") || starts("B77") || starts("B78") || starts("A35") || starts("A33") || starts("A34") || starts("B76") || in({ "MD11", "DC10", "C17", "C5M", "A124", "A225", "K35R", "KC10", "B52" })) r = { Icon::Airliner, 1.25f };
        else if (in({ "A318", "A319", "A320", "A321", "A20N", "A21N", "A19N", "B737", "B738", "B739", "B38M", "B39M", "B37M", "B733", "B734", "B735", "B736", "B752", "B753", "BCS1", "BCS3", "E170", "E75L", "E75S", "E190", "E195", "E290", "E295", "E145", "E135", "CRJ2", "CRJ7", "CRJ9", "CRJX", "MD82", "MD83", "MD88", "MD90", "B712", "B722", "B732", "RJ85", "F100" })) r = { Icon::Airliner, 1.0f };
        it = cache.emplace(type, r).first;
    }
    if (sizeScale) *sizeScale = it->second.second;
    return it->second.first;
}

// category: 0 cargo, 1 tanker, 2 passenger, 3 fishing, 4 other (see ais::category); shipType is the raw AIS type code.
inline Icon shipIcon(int category, int shipType) {
    if (shipType == 36 || shipType == 37) return Icon::ShipSailing;
    if (shipType == 31 || shipType == 32 || shipType == 52 || shipType == 33 || shipType == 34 || shipType == 50 || shipType == 51 || shipType == 53) return Icon::ShipTug;
    switch (category) {
        case 0: return Icon::ShipCargo;
        case 1: return Icon::ShipTanker;
        case 2: return Icon::ShipPassenger;
        case 3: return Icon::ShipFishing;
        default: return Icon::ShipTug;
    }
}

// `name` is the catalogue name, `group` the group from satGroup(), `category` the 0..5 filter category.
inline Icon satelliteIcon(const std::string& name, int category, bool isShipKind) {
    auto has = [&](const char* k) { return name.find(k) != std::string::npos; };
    if (has("ZARYA") || has("ISS") || has("TIANHE") || has("CSS") || has("MENGTIAN") || has("WENTIAN")) return Icon::SatStation;
    if (name == "HST" || has("HUBBLE") || has("JWST") || has("CHANDRA") || has("SPITZER") || has("XMM") || has("FERMI") || has("TESS")) return Icon::SatTelescope;
    if (isShipKind || has("DRAGON") || has("SOYUZ") || has("SHENZHOU") || has("STARLINER") || has("CYGNUS") || has("PROGRESS") || has("TIANZHOU")) return Icon::Capsule;
    switch (category) {
        case 0: return Icon::SatStarlink;
        case 2: return Icon::SatNav;
        case 3: return Icon::SatWeather;
        case 4: return Icon::SatStation;
        default: return Icon::SatGeneric;
    }
}
