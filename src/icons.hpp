#pragma once
// Vector silhouettes rasterised into a single sprite atlas at start-up (no external image files).
// Every icon is drawn pointing up (nose / bow towards the top). The atlas is shaded: each polygon carries a brightness, so
// cockpits, engines and hatch covers read darker and spines and decks lighter. It is multiplied by the tint when drawn.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"

enum class Icon {
    Airliner, Widebody, Jumbo, Regional, BizJet, LightPlane, Helicopter, Fighter, Airlifter,
    ShipCargo, ShipTanker, ShipPassenger, ShipFishing, ShipSailing, ShipYacht, ShipTug, ShipNaval,
    SatGeneric, SatStarlink, SatStation, SatTelescope, SatNav, SatWeather, Capsule, Probe, Rocket,
    Disc,
    Count
};

namespace icons_detail {
struct Poly { std::vector<Vector2> pts; unsigned char alpha; unsigned char shade = 255; };
using Shape = std::vector<Poly>;

inline Poly P(std::initializer_list<Vector2> pts, unsigned char a = 255, unsigned char shade = 255) { Poly p; p.pts = std::vector<Vector2>(pts); p.alpha = a; p.shade = shade; return p; }
inline Poly mirrored(const Poly& p) { Poly m = p; for (Vector2& v : m.pts) v.x = -v.x; std::reverse(m.pts.begin(), m.pts.end()); return m; }
inline void both(Shape& s, const Poly& p) { s.push_back(p); s.push_back(mirrored(p)); }
// A polygon from its right-hand outline (listed nose to tail) plus the mirror image: fuselages and hulls.
inline Poly symm(std::initializer_list<Vector2> right, unsigned char a = 255, unsigned char shade = 255) {
    Poly p; p.alpha = a; p.shade = shade;
    const std::vector<Vector2> r(right);
    for (const Vector2& v : r) p.pts.push_back(v);
    for (int i = (int)r.size() - 1; i >= 0; --i) p.pts.push_back({ -r[(size_t)i].x, r[(size_t)i].y });
    return p;
}
inline Poly rect(float x0, float y0, float x1, float y1, unsigned char a = 255, unsigned char shade = 255) { return P({ { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }, a, shade); }
inline Poly bar(float cx, float cy, float len, float width, float deg, unsigned char a = 255, unsigned char shade = 255) {
    const float r = deg * 3.14159265f / 180.0f, dx = std::cos(r) * len * 0.5f, dy = std::sin(r) * len * 0.5f, nx = -std::sin(r) * width * 0.5f, ny = std::cos(r) * width * 0.5f;
    return P({ { cx - dx - nx, cy - dy - ny }, { cx + dx - nx, cy + dy - ny }, { cx + dx + nx, cy + dy + ny }, { cx - dx + nx, cy - dy + ny } }, a, shade);
}
inline Poly ellipse(float cx, float cy, float rx, float ry, unsigned char a = 255, unsigned char shade = 255) {
    Poly p; p.alpha = a; p.shade = shade;
    for (int i = 0; i < 24; ++i) { const float t = i * 6.2831853f / 24.0f; p.pts.push_back({ cx + std::cos(t) * rx, cy + std::sin(t) * ry }); }
    return p;
}
// A stack of containers: `cols` columns by `rows` rows of slightly different brightness, with thin gaps.
inline void containers(Shape& s, float x0, float x1, float y0, float y1, int cols, int rows, int seed) {
    const float cw = (x1 - x0) / cols, ch = (y1 - y0) / rows;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            static const unsigned char tones[] = { 235, 175, 205, 150, 225, 190, 245, 165 };
            const unsigned char t = tones[(size_t)((r * 5 + c * 3 + seed) % 8)];
            s.push_back(rect(x0 + c * cw + 0.012f, y0 + r * ch + 0.012f, x0 + (c + 1) * cw - 0.012f, y0 + (r + 1) * ch - 0.012f, 255, t));
        }
}

inline Shape shapeFor(Icon icon) {
    Shape s;
    switch (icon) {
        case Icon::Airliner:  // narrow-body jet: swept wings, underwing engines, tailplane
            s.push_back(symm({ { 0, -1 }, { 0.03f, -0.97f }, { 0.062f, -0.88f }, { 0.078f, -0.7f }, { 0.082f, -0.4f }, { 0.082f, 0.45f }, { 0.07f, 0.75f }, { 0.04f, 0.93f }, { 0, 0.99f } }, 255, 232));
            both(s, P({ { 0.07f, -0.2f }, { 0.99f, 0.34f }, { 0.99f, 0.45f }, { 0.07f, 0.1f } }, 255, 200));
            both(s, P({ { 0.97f, 0.26f }, { 1.0f, 0.18f }, { 1.0f, 0.43f }, { 0.97f, 0.45f } }, 255, 150));  // winglets
            both(s, P({ { 0.06f, 0.73f }, { 0.39f, 0.93f }, { 0.39f, 0.99f }, { 0.05f, 0.91f } }, 255, 200));
            both(s, P({ { 0.31f, -0.08f }, { 0.345f, -0.14f }, { 0.38f, -0.08f }, { 0.38f, 0.08f }, { 0.31f, 0.08f } }, 255, 110));
            s.push_back(rect(-0.014f, -0.9f, 0.014f, 0.85f, 255, 255));  // bright spine
            s.push_back(symm({ { 0, -0.91f }, { 0.04f, -0.85f }, { 0.03f, -0.8f }, { 0, -0.79f } }, 255, 60));  // cockpit windows
            break;
        case Icon::Widebody:  // twin-aisle jet: fatter fuselage, longer wings, big engines
            s.push_back(symm({ { 0, -1 }, { 0.04f, -0.97f }, { 0.08f, -0.88f }, { 0.098f, -0.7f }, { 0.1f, -0.4f }, { 0.1f, 0.45f }, { 0.085f, 0.75f }, { 0.045f, 0.93f }, { 0, 0.99f } }, 255, 232));
            both(s, P({ { 0.09f, -0.3f }, { 1.0f, 0.38f }, { 1.0f, 0.5f }, { 0.09f, 0.16f } }, 255, 200));
            both(s, P({ { 0.98f, 0.3f }, { 1.0f, 0.22f }, { 1.0f, 0.5f }, { 0.98f, 0.52f } }, 255, 150));
            both(s, P({ { 0.08f, 0.72f }, { 0.45f, 0.94f }, { 0.45f, 1.0f }, { 0.07f, 0.92f } }, 255, 200));
            both(s, P({ { 0.34f, -0.18f }, { 0.4f, -0.25f }, { 0.46f, -0.18f }, { 0.46f, 0.04f }, { 0.34f, 0.04f } }, 255, 105));
            s.push_back(rect(-0.016f, -0.92f, 0.016f, 0.85f, 255, 255));
            s.push_back(symm({ { 0, -0.92f }, { 0.05f, -0.85f }, { 0.04f, -0.8f }, { 0, -0.79f } }, 255, 60));
            break;
        case Icon::Jumbo:  // four engines and the upper-deck hump
            s.push_back(symm({ { 0, -1 }, { 0.04f, -0.97f }, { 0.08f, -0.88f }, { 0.1f, -0.7f }, { 0.105f, -0.4f }, { 0.105f, 0.45f }, { 0.088f, 0.75f }, { 0.045f, 0.93f }, { 0, 0.99f } }, 255, 230));
            both(s, P({ { 0.1f, -0.34f }, { 1.0f, 0.42f }, { 1.0f, 0.54f }, { 0.1f, 0.18f } }, 255, 200));
            both(s, P({ { 0.98f, 0.34f }, { 1.0f, 0.26f }, { 1.0f, 0.54f }, { 0.98f, 0.56f } }, 255, 150));
            both(s, P({ { 0.09f, 0.72f }, { 0.46f, 0.94f }, { 0.46f, 1.0f }, { 0.08f, 0.92f } }, 255, 200));
            s.push_back(symm({ { 0, -0.78f }, { 0.07f, -0.72f }, { 0.118f, -0.55f }, { 0.12f, -0.3f }, { 0.1f, -0.16f }, { 0, -0.16f } }, 255, 252));  // hump
            both(s, P({ { 0.23f, -0.3f }, { 0.27f, -0.36f }, { 0.31f, -0.3f }, { 0.31f, -0.1f }, { 0.23f, -0.1f } }, 255, 105));
            both(s, P({ { 0.58f, -0.04f }, { 0.62f, -0.1f }, { 0.66f, -0.04f }, { 0.66f, 0.16f }, { 0.58f, 0.16f } }, 255, 105));
            s.push_back(symm({ { 0, -0.93f }, { 0.04f, -0.87f }, { 0.03f, -0.82f }, { 0, -0.81f } }, 255, 60));
            break;
        case Icon::Regional:  // turboprop / regional: straight wing, propeller discs, T-tail
            s.push_back(symm({ { 0, -0.95f }, { 0.03f, -0.9f }, { 0.062f, -0.75f }, { 0.07f, -0.3f }, { 0.07f, 0.5f }, { 0.04f, 0.88f }, { 0, 0.94f } }, 255, 232));
            both(s, P({ { 0.06f, -0.1f }, { 0.98f, -0.02f }, { 0.98f, 0.14f }, { 0.06f, 0.18f } }, 255, 200));
            both(s, P({ { 0.05f, 0.74f }, { 0.38f, 0.82f }, { 0.38f, 0.95f }, { 0.04f, 0.93f } }, 255, 200));
            both(s, P({ { 0.3f, -0.24f }, { 0.38f, -0.24f }, { 0.38f, 0.1f }, { 0.3f, 0.1f } }, 255, 110));  // nacelles
            both(s, ellipse(0.34f, -0.3f, 0.12f, 0.04f, 110, 235));  // propeller discs
            s.push_back(symm({ { 0, -0.86f }, { 0.04f, -0.8f }, { 0.03f, -0.75f }, { 0, -0.74f } }, 255, 60));
            break;
        case Icon::BizJet:  // slim body, rear engines, T-tail
            s.push_back(symm({ { 0, -1 }, { 0.03f, -0.95f }, { 0.055f, -0.8f }, { 0.062f, -0.4f }, { 0.062f, 0.4f }, { 0.04f, 0.85f }, { 0, 0.94f } }, 255, 236));
            both(s, P({ { 0.055f, 0.0f }, { 0.78f, 0.5f }, { 0.78f, 0.62f }, { 0.055f, 0.36f } }, 255, 205));
            both(s, P({ { 0.04f, 0.72f }, { 0.32f, 0.95f }, { 0.32f, 1.0f }, { 0.04f, 0.92f } }, 255, 205));
            both(s, rect(0.1f, 0.48f, 0.19f, 0.86f, 255, 105));  // rear-mounted engines
            s.push_back(symm({ { 0, -0.9f }, { 0.03f, -0.84f }, { 0.02f, -0.8f }, { 0, -0.79f } }, 255, 60));
            break;
        case Icon::LightPlane:  // high straight wing, propeller
            s.push_back(symm({ { 0, -0.8f }, { 0.045f, -0.72f }, { 0.07f, -0.5f }, { 0.07f, 0.3f }, { 0.04f, 0.8f }, { 0, 0.9f } }, 255, 236));
            both(s, P({ { 0.06f, -0.32f }, { 1.0f, -0.32f }, { 1.0f, -0.06f }, { 0.06f, -0.04f } }, 255, 208));
            both(s, P({ { 0.03f, 0.72f }, { 0.4f, 0.74f }, { 0.4f, 0.9f }, { 0.03f, 0.88f } }, 255, 208));
            s.push_back(bar(0, -0.84f, 0.95f, 0.04f, 0, 140, 235));
            s.push_back(ellipse(0, -0.8f, 0.05f, 0.06f, 255, 100));
            s.push_back(symm({ { 0, -0.55f }, { 0.04f, -0.48f }, { 0.04f, -0.3f }, { 0, -0.3f } }, 255, 70));
            break;
        case Icon::Helicopter:  // main rotor disc and blades, tail boom, tail rotor
            s.push_back(ellipse(0, -0.05f, 0.97f, 0.97f, 38, 235));
            s.push_back(rect(-0.03f, 0.2f, 0.03f, 0.84f, 255, 215));
            s.push_back(ellipse(0, -0.05f, 0.17f, 0.38f, 255, 235));
            s.push_back(ellipse(0, -0.3f, 0.1f, 0.12f, 255, 70));
            s.push_back(rect(-0.2f, 0.74f, 0.2f, 0.84f, 255, 200));
            s.push_back(bar(0, 0.82f, 0.3f, 0.05f, 90, 200, 120));
            s.push_back(bar(0, -0.05f, 1.9f, 0.06f, 32, 215, 105));
            s.push_back(bar(0, -0.05f, 1.9f, 0.06f, -58, 215, 105));
            s.push_back(ellipse(0, -0.05f, 0.07f, 0.07f, 255, 60));
            break;
        case Icon::Fighter:  // delta wings, twin tails, canopy
            s.push_back(symm({ { 0, -1 }, { 0.03f, -0.8f }, { 0.06f, -0.5f }, { 0.085f, -0.1f }, { 0.09f, 0.5f }, { 0.06f, 0.95f }, { 0, 0.9f } }, 255, 225));
            both(s, P({ { 0.08f, -0.15f }, { 0.9f, 0.62f }, { 0.9f, 0.8f }, { 0.08f, 0.6f } }, 255, 195));
            both(s, P({ { 0.07f, 0.62f }, { 0.38f, 0.9f }, { 0.38f, 1.0f }, { 0.06f, 0.92f } }, 255, 195));
            both(s, rect(0.075f, -0.2f, 0.13f, 0.12f, 255, 85));  // intakes
            s.push_back(ellipse(0, -0.42f, 0.04f, 0.17f, 255, 55));
            s.push_back(rect(-0.05f, 0.86f, 0.05f, 1.0f, 255, 80));
            break;
        case Icon::Airlifter:  // military transport: bulbous nose, high straight wing with four engines, T-tail
            s.push_back(symm({ { 0, -1 }, { 0.05f, -0.95f }, { 0.1f, -0.8f }, { 0.11f, -0.55f }, { 0.11f, 0.6f }, { 0.08f, 0.82f }, { 0, 0.95f } }, 255, 222));
            both(s, P({ { 0.1f, -0.22f }, { 1.0f, -0.16f }, { 1.0f, 0.04f }, { 0.1f, 0.08f } }, 255, 200));
            both(s, P({ { 0.08f, 0.7f }, { 0.45f, 0.78f }, { 0.45f, 0.95f }, { 0.07f, 0.93f } }, 255, 200));
            for (float x : { 0.3f, 0.66f }) { both(s, rect(x - 0.04f, -0.34f, x + 0.04f, 0.02f, 255, 105)); both(s, ellipse(x, -0.38f, 0.1f, 0.03f, 100, 235)); }
            s.push_back(symm({ { 0, -0.92f }, { 0.06f, -0.85f }, { 0.05f, -0.8f }, { 0, -0.79f } }, 255, 60));
            s.push_back(rect(-0.05f, 0.35f, 0.05f, 0.8f, 255, 190));  // cargo ramp
            break;
        case Icon::ShipCargo:  // container ship: stacked boxes forward and aft, bridge and funnel at the stern
            s.push_back(symm({ { 0, -1 }, { 0.08f, -0.93f }, { 0.17f, -0.78f }, { 0.22f, -0.55f }, { 0.23f, 0.7f }, { 0.22f, 0.9f }, { 0.12f, 0.96f }, { 0, 0.96f } }, 255, 130));
            containers(s, -0.19f, 0.19f, -0.68f, -0.12f, 3, 4, 0);
            containers(s, -0.19f, 0.19f, -0.04f, 0.46f, 3, 4, 3);
            s.push_back(rect(-0.18f, 0.55f, 0.18f, 0.9f, 255, 250));
            s.push_back(rect(-0.16f, 0.57f, 0.16f, 0.62f, 255, 60));
            s.push_back(rect(-0.06f, 0.72f, 0.06f, 0.84f, 255, 120));
            break;
        case Icon::ShipTanker:  // long flat deck with pipework, bridge at the stern
            s.push_back(symm({ { 0, -1 }, { 0.1f, -0.94f }, { 0.2f, -0.8f }, { 0.25f, -0.55f }, { 0.26f, 0.78f }, { 0.22f, 0.95f }, { 0, 0.97f } }, 255, 165));
            s.push_back(rect(-0.022f, -0.85f, 0.022f, 0.5f, 255, 235));
            for (float y : { -0.6f, -0.35f, -0.1f, 0.15f, 0.4f }) s.push_back(rect(-0.2f, y, 0.2f, y + 0.035f, 255, 210));
            s.push_back(ellipse(0, -0.84f, 0.06f, 0.05f, 255, 120));
            s.push_back(rect(-0.2f, 0.6f, 0.2f, 0.93f, 255, 250));
            s.push_back(rect(-0.18f, 0.62f, 0.18f, 0.67f, 255, 60));
            s.push_back(rect(-0.05f, 0.78f, 0.05f, 0.88f, 255, 110));
            break;
        case Icon::ShipPassenger:  // cruise ship / ferry: stepped decks, funnels, lifeboats
            s.push_back(symm({ { 0, -1 }, { 0.09f, -0.92f }, { 0.2f, -0.7f }, { 0.25f, -0.4f }, { 0.25f, 0.8f }, { 0.2f, 0.94f }, { 0, 0.96f } }, 255, 240));
            s.push_back(rect(-0.2f, -0.55f, 0.2f, 0.78f, 255, 255));
            s.push_back(rect(-0.16f, -0.38f, 0.16f, 0.62f, 255, 222));
            s.push_back(rect(-0.1f, -0.22f, 0.1f, 0.42f, 255, 255));
            s.push_back(rect(-0.07f, 0.02f, 0.07f, 0.12f, 255, 120));  // pool
            s.push_back(ellipse(0, 0.3f, 0.07f, 0.1f, 255, 95));       // funnel
            s.push_back(rect(-0.09f, -0.32f, 0.09f, -0.27f, 255, 70));  // bridge windows
            for (float y : { -0.45f, -0.2f, 0.05f, 0.3f, 0.55f }) both(s, rect(0.21f, y, 0.245f, y + 0.12f, 255, 180));  // lifeboats
            break;
        case Icon::ShipFishing:  // trawler: wheelhouse forward, working deck and A-frame gantry aft
            s.push_back(symm({ { 0, -0.95f }, { 0.15f, -0.7f }, { 0.27f, -0.3f }, { 0.28f, 0.7f }, { 0.2f, 0.85f }, { 0, 0.87f } }, 255, 195));
            s.push_back(rect(-0.16f, -0.42f, 0.16f, -0.05f, 255, 250));
            s.push_back(rect(-0.14f, -0.4f, 0.14f, -0.33f, 255, 60));
            s.push_back(rect(-0.2f, 0.0f, 0.2f, 0.55f, 255, 150));
            s.push_back(bar(0, 0.62f, 0.62f, 0.05f, 90, 255, 90));
            both(s, bar(0.24f, 0.72f, 0.3f, 0.04f, 80, 255, 90));
            s.push_back(bar(0, -0.1f, 0.012f, 0.9f, 0, 255, 70));
            break;
        case Icon::ShipSailing:  // sloop: slim hull, a billowed headsail forward and a long mainsail with its boom swung out aft
            s.push_back(symm({ { 0, -1 }, { 0.07f, -0.6f }, { 0.11f, 0.0f }, { 0.1f, 0.8f }, { 0.05f, 0.9f }, { 0, 0.9f } }, 255, 238));
            s.push_back(P({ { 0.0f, -0.95f }, { 0.14f, -0.62f }, { 0.3f, -0.3f }, { 0.26f, -0.1f }, { 0.02f, -0.2f } }, 245, 255));
            s.push_back(P({ { -0.01f, -0.15f }, { -0.12f, 0.2f }, { -0.26f, 0.55f }, { -0.3f, 0.8f }, { -0.02f, 0.72f } }, 235, 245));
            s.push_back(bar(-0.01f, 0.4f, 0.012f, 1.0f, 0, 255, 80));  // mast
            s.push_back(bar(-0.1f, 0.74f, 0.46f, 0.025f, 82, 255, 80));  // boom
            break;
        case Icon::ShipYacht:  // motor yacht: pointed bow, cabin, flybridge
            s.push_back(symm({ { 0, -1 }, { 0.09f, -0.75f }, { 0.15f, -0.2f }, { 0.16f, 0.6f }, { 0.13f, 0.9f }, { 0, 0.92f } }, 255, 245));
            s.push_back(symm({ { 0, -0.25f }, { 0.1f, -0.2f }, { 0.12f, 0.35f }, { 0, 0.4f } }, 255, 205));
            s.push_back(rect(-0.09f, -0.27f, 0.09f, -0.22f, 255, 60));
            s.push_back(rect(-0.07f, -0.05f, 0.07f, 0.22f, 255, 255));
            s.push_back(rect(-0.1f, 0.5f, 0.1f, 0.8f, 255, 175));  // sundeck
            break;
        case Icon::ShipTug:  // short, beamy hull, tall wheelhouse, tow bitt
            s.push_back(symm({ { 0, -0.9f }, { 0.22f, -0.65f }, { 0.32f, -0.2f }, { 0.32f, 0.6f }, { 0.22f, 0.85f }, { 0, 0.87f } }, 255, 185));
            s.push_back(rect(-0.2f, -0.25f, 0.2f, 0.3f, 255, 250));
            s.push_back(rect(-0.18f, -0.22f, 0.18f, -0.14f, 255, 60));
            s.push_back(rect(-0.07f, 0.12f, 0.07f, 0.28f, 255, 100));
            s.push_back(ellipse(0, 0.62f, 0.08f, 0.08f, 255, 80));
            s.push_back(ellipse(0, -0.7f, 0.07f, 0.06f, 255, 90));
            break;
        case Icon::ShipNaval:  // warship: slender grey hull, bow gun, superstructure with mast, helicopter deck
            s.push_back(symm({ { 0, -1 }, { 0.05f, -0.9f }, { 0.13f, -0.6f }, { 0.18f, -0.2f }, { 0.19f, 0.5f }, { 0.15f, 0.9f }, { 0, 0.95f } }, 255, 140));
            s.push_back(ellipse(0, -0.62f, 0.05f, 0.06f, 255, 225));
            s.push_back(rect(-0.01f, -0.78f, 0.01f, -0.62f, 255, 225));
            s.push_back(rect(-0.05f, -0.5f, 0.05f, -0.38f, 255, 105));  // missile cells
            s.push_back(symm({ { 0, -0.3f }, { 0.09f, -0.25f }, { 0.1f, 0.25f }, { 0, 0.3f } }, 255, 232));
            s.push_back(rect(-0.05f, 0.14f, 0.05f, 0.3f, 255, 105));
            s.push_back(rect(-0.012f, -0.28f, 0.012f, 0.05f, 255, 255));  // mast
            s.push_back(ellipse(0, 0.66f, 0.1f, 0.1f, 255, 185));  // helicopter deck
            s.push_back(ellipse(0, 0.66f, 0.055f, 0.055f, 255, 110));
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
        case Icon::Disc: s.push_back(ellipse(0, 0, 0.95f, 0.95f)); break;
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
    int cell = 128, cols = 8;
    bool ok = false;

    void build() {
        using namespace icons_detail;
        const int n = (int)Icon::Count, rows = (n + cols - 1) / cols, W = cols * cell, H = rows * cell, SS = 2;
        std::vector<unsigned char> px((size_t)W * H * 4, 0);
        for (int ic = 0; ic < n; ++ic) {
            const Shape shape = shapeFor((Icon)ic);
            struct Box { float x0, y0, x1, y1; };
            std::vector<Box> boxes;
            for (const Poly& p : shape) {
                Box b{ 9, 9, -9, -9 };
                for (const Vector2& v : p.pts) { b.x0 = std::min(b.x0, v.x); b.y0 = std::min(b.y0, v.y); b.x1 = std::max(b.x1, v.x); b.y1 = std::max(b.y1, v.y); }
                boxes.push_back(b);
            }
            const int ox = (ic % cols) * cell, oy = (ic / cols) * cell;
            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x) {
                    float sumA = 0.0f, sumC = 0.0f;
                    for (int sy = 0; sy < SS; ++sy)
                        for (int sx = 0; sx < SS; ++sx) {
                            const float fx = ((x + (sx + 0.5f) / SS) / cell - 0.5f) * 2.0f / 0.92f, fy = ((y + (sy + 0.5f) / SS) / cell - 0.5f) * 2.0f / 0.92f;
                            float a = 0.0f, c = 255.0f;
                            for (size_t k = 0; k < shape.size(); ++k) {  // later polygons draw over earlier ones
                                const Box& b = boxes[k];
                                if (fx < b.x0 || fx > b.x1 || fy < b.y0 || fy > b.y1) continue;
                                if (inside(shape[k].pts, fx, fy)) { a = shape[k].alpha / 255.0f; c = (float)shape[k].shade; }
                            }
                            sumA += a; sumC += a * c;
                        }
                    const float cover = sumA / (SS * SS);
                    const float shade = sumA > 0.0f ? sumC / sumA : 255.0f;
                    unsigned char* d = &px[((size_t)(oy + y) * W + ox + x) * 4];
                    d[0] = d[1] = d[2] = (unsigned char)std::min(255.0f, shade);
                    d[3] = (unsigned char)std::min(255.0f, cover * 255.0f);
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
        if (size >= 13.0f) draw(icon, at, size * 1.28f, rotationDeg, Fade(BLACK, 0.6f));  // tiny icons skip the halo to save fill and draw calls
        draw(icon, at, size, rotationDeg, tint);
    }
    // Aircraft: a soft shadow that slides away from the aircraft the higher it flies (alt01 is 0 on the ground, 1 at about FL400).
    void drawAircraft(Icon icon, Vector2 at, float size, float rotationDeg, Color tint, float alt01) const {
        if (size >= 11.0f) {
            if (size >= 20.0f) { Color glow = tint; glow.a = (unsigned char)(tint.a * 0.17f); draw(icon, at, size * 1.38f, rotationDeg, glow); }  // a faint tinted glow, so the traffic reads like a lit display; skipped on small icons to keep thousands of them fast
            const float off = size * (0.07f + 0.30f * alt01);
            draw(icon, { at.x + off * 0.7f, at.y + off * 0.9f }, size * (1.04f + 0.08f * alt01), rotationDeg, Fade(BLACK, 0.50f - 0.18f * alt01));
        }
        draw(icon, at, size, rotationDeg, tint);
    }
    // Ships: a wake that lengthens with speed (speedKn in knots), a tight shadow, then the hull.
    void drawShip(Icon icon, Vector2 at, float size, float rotationDeg, Color tint, float speedKn) const {
        if (size >= 11.0f) {
            const float r = rotationDeg * 3.14159265f / 180.0f, fx = std::sin(r), fy = -std::cos(r);
            if (speedKn > 1.5f) {
                const float len = size * (0.6f + std::min(speedKn, 26.0f) * 0.05f), w = size * (0.16f + std::min(speedKn, 26.0f) * 0.006f);
                const Vector2 stern{ at.x - fx * size * 0.45f, at.y - fy * size * 0.45f }, px{ -fy, fx };
                for (int k = 0; k < 3; ++k) {
                    const float f = 1.0f - k * 0.28f;
                    DrawTriangle({ stern.x + px.x * w * f, stern.y + px.y * w * f }, { stern.x - px.x * w * f, stern.y - px.y * w * f }, { stern.x - fx * len * f * 1.0f, stern.y - fy * len * f * 1.0f }, Fade(WHITE, 0.10f));
                    DrawTriangle({ stern.x - px.x * w * f, stern.y - px.y * w * f }, { stern.x + px.x * w * f, stern.y + px.y * w * f }, { stern.x - fx * len * f * 1.0f, stern.y - fy * len * f * 1.0f }, Fade(WHITE, 0.10f));
                }
            }
            if (size >= 20.0f) { Color glow = tint; glow.a = (unsigned char)(tint.a * 0.17f); draw(icon, at, size * 1.38f, rotationDeg, glow); }
            draw(icon, { at.x + size * 0.05f, at.y + size * 0.07f }, size * 1.06f, rotationDeg, Fade(BLACK, 0.5f));
        }
        draw(icon, at, size, rotationDeg, tint);
    }
};

// ---- Choosing an icon from catalogue data --------------------------------------------------------------------------
// Surface vehicles that ADS-B reports as aircraft (towers, tugs, fire trucks): drawn as a small marker, not an aeroplane.
inline bool isGroundVehicleType(const std::string& type) { return type == "TWR" || type == "GND" || type == "GRND" || type == "SERV" || type == "EMER" || type == "UAV"; }

// groundSpeedKt (when known) picks a light plane over an airliner for type codes the lists do not cover.
inline Icon aircraftIcon(const std::string& type, float* sizeScale = nullptr, double groundSpeedKt = -1.0) {
    static std::unordered_map<std::string, std::pair<Icon, float>> cache;
    auto it = cache.find(type);
    if (it == cache.end()) {
        auto in = [&](std::initializer_list<const char*> list) { for (const char* s : list) if (type == s) return true; return false; };
        auto starts = [&](const char* p) { return type.rfind(p, 0) == 0; };
        std::pair<Icon, float> r{ Icon::Count, 1.0f };  // Count = not in any list; decided from speed below
        if (in({ "R44", "R22", "R66", "EC35", "EC30", "EC20", "EC45", "EC55", "A139", "A109", "A119", "S76", "S92", "S61", "B06", "B407", "B412", "B429", "B505", "H60", "H47", "H64", "AS50", "AS55", "AS65", "MD52", "MD60", "H500", "EN28", "EN48", "B212", "B222", "S58", "H145", "LYNX", "AS32", "AS3B", "A169", "H160", "EH10", "BK17", "EC25", "H125", "H135", "NH90", "EC75", "AS65", "SA330", "PUMA", "SK76", "S70", "AW09", "AW189", "A189" })) r = { Icon::Helicopter, 1.0f };
        else if (starts("F16") || starts("F15") || starts("F18") || starts("F22") || starts("F35") || in({ "T38", "A10", "EUFI", "RFAL", "TORN", "GRIP", "F5", "T6", "FA18", "HAWK", "M346" })) r = { Icon::Fighter, 0.9f };
        else if (in({ "C150", "C152", "C172", "C177", "C182", "C185", "C206", "C210", "C170", "C140", "PA28", "PA32", "PA34", "PA44", "PA46", "PA18", "PA38", "P28A", "P28B", "SR20", "SR22", "DA40", "DA42", "DA62", "M20P", "M20T", "BE33", "BE35", "BE36", "BE55", "BE58", "AA5", "G115", "RV6", "RV7", "RV8", "RV10", "COZY", "J3", "T206", "T210", "SIRA", "TOBA", "BL8", "AC11", "P180", "GLID", "ULAC", "BALL", "PC6T", "DR40", "DR400", "A210", "P208", "WT9", "SLG4", "SLG2", "S22T", "P06T", "D250", "C421", "C42", "C82T", "Z42", "TB20", "TB10", "TB21", "SHRK", "PIVI", "PA30", "PA23", "P46T", "P28R", "P28T", "KP2", "FK9", "DV20", "CRUZ", "BREZ", "AC50", "AC11", "EVSS", "AP32", "BT36", "C441", "C404", "C303", "T34P", "T6" })) r = { Icon::LightPlane, 0.85f };
        else if (in({ "C17", "C5M", "C130", "C30J", "A400", "C27J", "C295", "CN35" })) r = { Icon::Airlifter, 1.15f };
        else if (in({ "AT72", "AT76", "AT75", "AT45", "AT46", "DH8A", "DH8B", "DH8C", "DH8D", "SF34", "SB20", "E120", "BE20", "BE9L", "BE10", "B190", "PC12", "C208", "TBM7", "TBM8", "TBM9", "JS32", "JS41", "D228", "DHC6", "DHC2", "L410", "SW4", "SH36", "C130", "C30J", "A400", "CN35", "C295", "P3", "E2", "E2C", "B350", "PAY1", "PAY2", "PAY3", "C27J", "SF50", "DH3T", "SB20", "B190", "BE99", "E110", "F27", "F50", "F60" })) r = { Icon::Regional, 1.0f };
        else if (starts("C25") || starts("C51") || starts("C52") || starts("C55") || starts("C56") || starts("C68") || starts("C70") || starts("C75") || starts("GLF") || starts("GL") || starts("GA") || starts("G2") || starts("G1") || starts("CL") || starts("LJ") || starts("FA") || starts("E50") || starts("E55") ||
                 in({ "F900", "F2TH", "H25B", "H25C", "PC24", "HDJT", "BE40", "PRM1", "SF50", "EA50", "LJ45", "G280", "FA7X", "FA8X" })) r = { Icon::BizJet, 0.95f };
        else if (starts("B74") || starts("A38") || in({ "A124", "A225", "B52" })) r = { Icon::Jumbo, 1.3f };
        else if (starts("B77") || starts("B78") || starts("A35") || starts("A33") || starts("A34") || starts("B76") || in({ "MD11", "DC10", "K35R", "KC10" })) r = { Icon::Widebody, 1.2f };
        else if (in({ "A318", "A319", "A320", "A321", "A20N", "A21N", "A19N", "B737", "B738", "B739", "B38M", "B39M", "B37M", "B733", "B734", "B735", "B736", "B752", "B753", "BCS1", "BCS3", "E170", "E75L", "E75S", "E190", "E195", "E290", "E295", "E145", "E135", "CRJ2", "CRJ7", "CRJ9", "CRJX", "MD82", "MD83", "MD88", "MD90", "B712", "B722", "B732", "RJ85", "F100" })) r = { Icon::Airliner, 1.0f };
        it = cache.emplace(type, r).first;
    }
    if (it->second.first == Icon::Count) {
        if (sizeScale) *sizeScale = groundSpeedKt > 0 && groundSpeedKt < 170 ? 0.85f : 1.0f;
        return groundSpeedKt > 0 && groundSpeedKt < 170 ? Icon::LightPlane : Icon::Airliner;
    }
    if (sizeScale) *sizeScale = it->second.second;
    return it->second.first;
}

// category: 0 cargo, 1 tanker, 2 passenger, 3 fishing, 4 other (see ais::category); shipType is the raw AIS type code.
inline Icon shipIcon(int category, int shipType) {
    if (shipType == 35 || shipType == 55) return Icon::ShipNaval;  // military operations, law enforcement
    if (shipType == 36) return Icon::ShipSailing;
    if (shipType == 37) return Icon::ShipYacht;
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
