#pragma once
// Low-poly 3D models of common spacecraft, built from primitives (no model files). Drawn inside a 3D pass; the model
// space has +Y pointing away from the Earth, +Z along the direction of travel and +X completing the right-handed frame.
// Models are roughly 2 units across and are scaled up by the caller so they stay a readable size at any zoom.
#include <cmath>
#include <cstdlib>
#include <string>

#include "icons.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

enum class ModelKind {
    Generic, Station, Tiangong, Telescope, Jwst, Capsule, Soyuz, Cargo, Starlink, Iridium, Nav, Weather, ComSat, RocketBody, Debris
};

// Chooses the model for a catalogue entry. oclass: 0 LEO, 1 MEO, 2 GEO, 3 HEO. category: the 0..5 filter category.
inline ModelKind modelKindFor(const std::string& name, int category, bool isShipKind, int oclass) {
    auto has = [&](const char* k) { return name.find(k) != std::string::npos; };
    if (has("TIANHE") || has("CSS") || has("TIANGONG") || has("MENGTIAN") || has("WENTIAN")) return ModelKind::Tiangong;
    if (has("ZARYA") || has("ISS")) return ModelKind::Station;
    if (has("JWST") || has("WEBB")) return ModelKind::Jwst;
    if (name == "HST" || has("HUBBLE") || has("CHANDRA") || has("SPITZER") || has("XMM") || has("FERMI") || has("TESS")) return ModelKind::Telescope;
    if (has("SOYUZ") || has("PROGRESS") || has("SHENZHOU")) return ModelKind::Soyuz;
    if (has("CYGNUS") || has("TIANZHOU") || has("HTV") || has("DRAGON CARGO")) return ModelKind::Cargo;
    if (isShipKind || has("DRAGON") || has("STARLINER") || has("ORION")) return ModelKind::Capsule;
    if (has(" R/B") || has("ROCKET") || has("CENTAUR") || has("FALCON 9") || has("H-2A") || has("CZ-")) return ModelKind::RocketBody;
    if (has(" DEB") || has("FRAGMENT")) return ModelKind::Debris;
    if (has("IRIDIUM")) return ModelKind::Iridium;
    if (has("STARLINK") || has("ONEWEB") || has("KUIPER") || category == 0) return ModelKind::Starlink;
    if (category == 2) return ModelKind::Nav;
    if (category == 3) return ModelKind::Weather;
    if (oclass == 2) return ModelKind::ComSat;
    return ModelKind::Generic;
}

namespace models_detail {
inline Color shade(Color c, float k) { return { (unsigned char)std::min(255.0f, c.r * k), (unsigned char)std::min(255.0f, c.g * k), (unsigned char)std::min(255.0f, c.b * k), 255 }; }
inline void box(Vector3 c, Vector3 size, Color col) {
    DrawCubeV(c, size, col);
    DrawCubeWiresV(c, size, shade(col, 0.45f));
}
inline void tube(Vector3 a, Vector3 b, float ra, float rb, Color col, int sides = 14) { DrawCylinderEx(a, b, ra, rb, sides, col); DrawCylinderWiresEx(a, b, ra, rb, sides, shade(col, 0.5f)); }
// A box rotated by `deg` about an axis through its own centre.
inline void turnedBox(Vector3 c, Vector3 size, Vector3 axis, float deg, Color col) {
    rlPushMatrix();
    rlTranslatef(c.x, c.y, c.z);
    rlRotatef(deg, axis.x, axis.y, axis.z);
    box({ 0, 0, 0 }, size, col);
    rlPopMatrix();
}
}  // namespace models_detail

inline void drawSpacecraftModel(ModelKind kind, const Matrix& frame, float scale, Color tint) {
    using namespace models_detail;
    const Color body = { 214, 218, 226, 255 }, gold = { 205, 160, 70, 255 }, panel = { 38, 70, 150, 255 }, dark = { 70, 74, 86, 255 };
    const Color foil = { 226, 190, 96, 255 }, silver = { 190, 196, 210, 255 };
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(frame));
    rlScalef(scale, scale, scale);
    switch (kind) {
        case ModelKind::Station:  // truss, pressurised modules and four pairs of solar arrays
            box({ 0, 0, 0 }, { 2.0f, 0.07f, 0.07f }, body);
            tube({ 0, 0, -0.55f }, { 0, 0, 0.7f }, 0.13f, 0.13f, body);
            tube({ -0.35f, -0.05f, 0.15f }, { 0.35f, -0.05f, 0.15f }, 0.09f, 0.09f, shade(body, 0.9f));
            for (float x : { -0.88f, -0.6f, 0.6f, 0.88f }) { box({ x, 0.0f, 0.0f }, { 0.2f, 0.012f, 0.95f }, panel); box({ x, 0.0f, 0.0f }, { 0.012f, 0.012f, 1.0f }, dark); }
            box({ 0.2f, 0.13f, -0.25f }, { 0.5f, 0.02f, 0.16f }, shade(body, 0.8f));  // radiator
            break;
        case ModelKind::Tiangong:  // T-shaped: Tianhe core with the Wentian and Mengtian labs on a cross-axis, three arrays pairs
            tube({ 0, 0, -0.8f }, { 0, 0, 0.55f }, 0.13f, 0.13f, body);
            tube({ 0, 0, 0.55f }, { 0, 0, 0.75f }, 0.13f, 0.07f, dark);
            tube({ -0.62f, 0, 0.35f }, { 0.62f, 0, 0.35f }, 0.11f, 0.11f, shade(body, 0.92f));
            tube({ 0, 0.0f, -0.8f }, { 0, 0.0f, -0.88f }, 0.1f, 0.06f, gold);
            for (float x : { -0.62f, 0.62f }) { box({ x, 0.0f, 0.35f }, { 0.22f, 0.012f, 0.95f }, panel); box({ x, 0.0f, 0.35f }, { 0.012f, 0.012f, 1.0f }, dark); }
            for (float x : { -0.3f, 0.3f }) { box({ x, 0.0f, -0.45f }, { 0.4f, 0.012f, 0.55f }, panel); }
            break;
        case ModelKind::Telescope:  // Hubble-like tube with two solar wings
            tube({ 0, 0, -0.75f }, { 0, 0, 0.7f }, 0.2f, 0.2f, body);
            tube({ 0, 0, 0.7f }, { 0, 0, 0.95f }, 0.2f, 0.17f, dark);
            tube({ 0, 0, -0.95f }, { 0, 0, -0.75f }, 0.17f, 0.2f, gold);
            box({ 0.6f, 0, -0.15f }, { 0.75f, 0.012f, 0.3f }, panel); box({ -0.6f, 0, -0.15f }, { 0.75f, 0.012f, 0.3f }, panel);
            tube({ 0.2f, 0, -0.15f }, { 0.26f, 0, -0.15f }, 0.03f, 0.03f, dark);
            break;
        case ModelKind::Jwst: {  // gold hexagonal mirror on a bus, behind a wide five-layer sunshield
            const float r = 0.16f, d = 0.29f;
            const Vector2 seg[7] = { { 0, 0 }, { d, 0 }, { -d, 0 }, { d / 2, d * 0.87f }, { -d / 2, d * 0.87f }, { d / 2, -d * 0.87f }, { -d / 2, -d * 0.87f } };
            for (const Vector2& sg : seg) tube({ sg.x, sg.y + 0.35f, 0.5f }, { sg.x, sg.y + 0.35f, 0.56f }, r, r, foil, 6);
            tube({ 0, 0.35f, 0.5f }, { 0, 0.35f, 0.2f }, 0.04f, 0.04f, dark);
            box({ 0, 0.2f, 0.0f }, { 0.5f, 0.35f, 0.5f }, shade(body, 0.85f));
            for (int l = 0; l < 4; ++l) box({ 0, -0.12f - 0.03f * l, -0.15f }, { 1.9f - 0.05f * l, 0.01f, 1.0f - 0.04f * l }, l == 0 ? Color{ 150, 170, 220, 255 } : silver);
            box({ 0, -0.38f, -0.55f }, { 0.4f, 0.14f, 0.3f }, dark);
            box({ 0.0f, -0.38f, -0.85f }, { 0.45f, 0.012f, 0.22f }, panel);
            break;
        }
        case ModelKind::Capsule:  // cone capsule on a service module with solar wings
            tube({ 0, 0, 0.1f }, { 0, 0, 0.95f }, 0.42f, 0.03f, shade(tint, 0.95f));
            tube({ 0, 0, -0.65f }, { 0, 0, 0.1f }, 0.42f, 0.42f, body);
            tube({ 0, 0, -0.85f }, { 0, 0, -0.65f }, 0.14f, 0.3f, dark);
            box({ 0.85f, 0, -0.35f }, { 0.8f, 0.012f, 0.3f }, panel); box({ -0.85f, 0, -0.35f }, { 0.8f, 0.012f, 0.3f }, panel);
            break;
        case ModelKind::Soyuz:  // spherical orbital module, bell-shaped descent module, instrument module, two wide arrays
            DrawSphere({ 0, 0, 0.7f }, 0.28f, shade(body, 0.95f));
            tube({ 0, 0, 0.3f }, { 0, 0, 0.45f }, 0.1f, 0.1f, dark);
            tube({ 0, 0, 0.3f }, { 0, 0, -0.05f }, 0.3f, 0.2f, shade(tint, 0.9f));
            tube({ 0, 0, -0.05f }, { 0, 0, -0.6f }, 0.3f, 0.3f, body);
            tube({ 0, 0, -0.6f }, { 0, 0, -0.78f }, 0.3f, 0.1f, gold);
            box({ 0.85f, 0, -0.3f }, { 0.9f, 0.012f, 0.38f }, panel); box({ -0.85f, 0, -0.3f }, { 0.9f, 0.012f, 0.38f }, panel);
            break;
        case ModelKind::Cargo:  // pressurised cylinder with two circular arrays (Cygnus / Tianzhou style)
            tube({ 0, 0, -0.6f }, { 0, 0, 0.6f }, 0.3f, 0.3f, body);
            tube({ 0, 0, 0.6f }, { 0, 0, 0.78f }, 0.3f, 0.12f, shade(body, 0.8f));
            tube({ 0, 0, -0.78f }, { 0, 0, -0.6f }, 0.12f, 0.3f, gold);
            for (float x : { -0.62f, 0.62f }) { tube({ x, 0, -0.1f }, { x + (x < 0 ? -0.02f : 0.02f), 0, -0.1f }, 0.38f, 0.38f, panel, 20); }
            break;
        case ModelKind::Starlink:  // flat bus with one big array
            box({ 0, 0, 0 }, { 0.8f, 0.06f, 0.55f }, body);
            box({ 1.05f, 0, 0 }, { 1.3f, 0.012f, 0.5f }, panel);
            box({ -0.1f, -0.06f, 0 }, { 0.3f, 0.04f, 0.3f }, dark);
            break;
        case ModelKind::Iridium:  // triangular bus, two small wings and three big flat antennas fanned around the axis
            tube({ 0, 0, -0.5f }, { 0, 0, 0.5f }, 0.2f, 0.2f, body, 3);
            box({ 0.7f, 0, -0.2f }, { 0.7f, 0.012f, 0.3f }, panel); box({ -0.7f, 0, -0.2f }, { 0.7f, 0.012f, 0.3f }, panel);
            for (int k = 0; k < 3; ++k) turnedBox({ 0.0f, -0.05f, 0.45f }, { 1.0f, 0.012f, 0.45f }, { 0, 0, 1 }, 60.0f + k * 120.0f, shade(body, 0.8f));
            break;
        case ModelKind::Nav:
            box({ 0, 0, 0 }, { 0.45f, 0.45f, 0.55f }, gold);
            box({ 0.8f, 0, 0 }, { 0.95f, 0.012f, 0.4f }, panel); box({ -0.8f, 0, 0 }, { 0.95f, 0.012f, 0.4f }, panel);
            tube({ 0, 0.22f, 0 }, { 0, 0.42f, 0 }, 0.14f, 0.05f, body);
            break;
        case ModelKind::Weather:
            box({ 0, 0, 0 }, { 0.5f, 0.5f, 0.55f }, body);
            box({ 0.85f, 0, 0 }, { 1.05f, 0.012f, 0.4f }, panel);
            tube({ -0.25f, 0.15f, 0 }, { -0.62f, 0.15f, 0 }, 0.05f, 0.28f, shade(body, 0.85f));
            break;
        case ModelKind::ComSat:  // big geostationary bus: two long arrays, two Earth-facing dishes (-Y) and a boom
            box({ 0, 0, 0 }, { 0.6f, 0.6f, 0.8f }, foil);
            box({ 1.15f, 0, 0 }, { 1.45f, 0.012f, 0.5f }, panel); box({ -1.15f, 0, 0 }, { 1.45f, 0.012f, 0.5f }, panel);
            tube({ 0.45f, 0, 0 }, { 0.62f, 0, 0 }, 0.03f, 0.03f, dark); tube({ -0.45f, 0, 0 }, { -0.62f, 0, 0 }, 0.03f, 0.03f, dark);
            tube({ -0.2f, -0.3f, 0.15f }, { -0.2f, -0.62f, 0.15f }, 0.05f, 0.3f, shade(body, 0.9f));
            tube({ 0.2f, -0.3f, -0.15f }, { 0.2f, -0.55f, -0.15f }, 0.05f, 0.22f, shade(body, 0.9f));
            tube({ 0, 0.3f, 0 }, { 0, 0.55f, 0 }, 0.02f, 0.02f, dark);
            break;
        case ModelKind::RocketBody:  // spent upper stage: tank, interstage ring, engine bell
            tube({ 0, 0, -0.85f }, { 0, 0, 0.85f }, 0.22f, 0.22f, shade(body, 0.85f));
            tube({ 0, 0, 0.85f }, { 0, 0, 1.0f }, 0.22f, 0.12f, dark);
            tube({ 0, 0, -1.05f }, { 0, 0, -0.85f }, 0.09f, 0.2f, dark);
            tube({ 0, 0, 0.1f }, { 0, 0, 0.16f }, 0.24f, 0.24f, shade(tint, 0.9f));
            tube({ 0, 0, -0.45f }, { 0, 0, -0.39f }, 0.24f, 0.24f, shade(tint, 0.9f));
            break;
        case ModelKind::Debris:  // a few tumbling fragments
            turnedBox({ 0, 0, 0 }, { 0.5f, 0.05f, 0.3f }, { 0.3f, 1, 0.2f }, 35.0f, shade(body, 0.7f));
            turnedBox({ 0.35f, 0.1f, 0.2f }, { 0.2f, 0.18f, 0.12f }, { 1, 0.2f, 0.4f }, 60.0f, shade(dark, 1.2f));
            turnedBox({ -0.3f, -0.08f, -0.25f }, { 0.28f, 0.03f, 0.2f }, { 0.1f, 0.6f, 1 }, 20.0f, shade(gold, 0.7f));
            break;
        default:  // generic satellite
            box({ 0, 0, 0 }, { 0.5f, 0.5f, 0.6f }, gold);
            box({ 0.85f, 0, 0 }, { 0.95f, 0.012f, 0.42f }, panel); box({ -0.85f, 0, 0 }, { 0.95f, 0.012f, 0.42f }, panel);
            tube({ 0, 0.25f, 0 }, { 0, 0.5f, 0 }, 0.04f, 0.14f, body);
            break;
    }
    rlPopMatrix();
}

// Frame for a model at `pos` on a trajectory heading `velocityDir`: +Y radial (away from Earth), +Z along track.
inline Matrix spacecraftFrame(Vector3 pos, Vector3 velocityDir) {
    const Vector3 up = Vector3Normalize(pos);
    Vector3 fwd = Vector3Subtract(velocityDir, Vector3Scale(up, Vector3DotProduct(velocityDir, up)));
    if (Vector3Length(fwd) < 1e-6f) fwd = { 0, 0, 1 };
    fwd = Vector3Normalize(fwd);
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(up, fwd));
    Matrix m = MatrixIdentity();
    m.m0 = right.x; m.m1 = right.y; m.m2 = right.z;
    m.m4 = up.x;    m.m5 = up.y;    m.m6 = up.z;
    m.m8 = fwd.x;   m.m9 = fwd.y;   m.m10 = fwd.z;
    m.m12 = pos.x;  m.m13 = pos.y;  m.m14 = pos.z;
    return m;
}
