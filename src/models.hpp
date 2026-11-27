#pragma once
// Low-poly 3D models of common spacecraft, built from primitives (no model files). Drawn inside a 3D pass; the model
// space has +Y pointing away from the Earth, +Z along the direction of travel and +X completing the right-handed frame.
// Models are roughly 2 units across and are scaled up by the caller so they stay a readable size at any zoom.
#include <cmath>
#include <cstdlib>

#include "icons.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace models_detail {
inline Color shade(Color c, float k) { return { (unsigned char)std::min(255.0f, c.r * k), (unsigned char)std::min(255.0f, c.g * k), (unsigned char)std::min(255.0f, c.b * k), 255 }; }
inline void box(Vector3 c, Vector3 size, Color col) {
    DrawCubeV(c, size, col);
    DrawCubeWiresV(c, size, shade(col, 0.45f));
}
inline void tube(Vector3 a, Vector3 b, float ra, float rb, Color col) { DrawCylinderEx(a, b, ra, rb, 14, col); DrawCylinderWiresEx(a, b, ra, rb, 14, shade(col, 0.5f)); }
}  // namespace models_detail

inline void drawSpacecraftModel(Icon kind, const Matrix& frame, float scale, Color tint) {
    using namespace models_detail;
    const Color body = { 214, 218, 226, 255 }, gold = { 205, 160, 70, 255 }, panel = { 38, 70, 150, 255 }, dark = { 70, 74, 86, 255 };
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(frame));
    rlScalef(scale, scale, scale);
    switch (kind) {
        case Icon::SatStation:  // truss, pressurised modules and four pairs of solar arrays
            box({ 0, 0, 0 }, { 2.0f, 0.07f, 0.07f }, body);
            tube({ 0, 0, -0.55f }, { 0, 0, 0.7f }, 0.13f, 0.13f, body);
            tube({ -0.35f, -0.05f, 0.15f }, { 0.35f, -0.05f, 0.15f }, 0.09f, 0.09f, shade(body, 0.9f));
            for (float x : { -0.88f, -0.6f, 0.6f, 0.88f }) { box({ x, 0.0f, 0.0f }, { 0.2f, 0.012f, 0.95f }, panel); box({ x, 0.0f, 0.0f }, { 0.012f, 0.012f, 1.0f }, dark); }
            box({ 0.2f, 0.13f, -0.25f }, { 0.5f, 0.02f, 0.16f }, shade(body, 0.8f));  // radiator
            break;
        case Icon::SatTelescope:  // Hubble-like tube with two solar wings
            tube({ 0, 0, -0.75f }, { 0, 0, 0.7f }, 0.2f, 0.2f, body);
            tube({ 0, 0, 0.7f }, { 0, 0, 0.95f }, 0.2f, 0.17f, dark);
            tube({ 0, 0, -0.95f }, { 0, 0, -0.75f }, 0.17f, 0.2f, gold);
            box({ 0.6f, 0, -0.15f }, { 0.75f, 0.012f, 0.3f }, panel); box({ -0.6f, 0, -0.15f }, { 0.75f, 0.012f, 0.3f }, panel);
            tube({ 0.2f, 0, -0.15f }, { 0.26f, 0, -0.15f }, 0.03f, 0.03f, dark);
            break;
        case Icon::Capsule:  // cone capsule on a service module with solar wings
            tube({ 0, 0, 0.1f }, { 0, 0, 0.95f }, 0.42f, 0.03f, shade(tint, 0.95f));
            tube({ 0, 0, -0.65f }, { 0, 0, 0.1f }, 0.42f, 0.42f, body);
            tube({ 0, 0, -0.85f }, { 0, 0, -0.65f }, 0.14f, 0.3f, dark);
            box({ 0.85f, 0, -0.35f }, { 0.8f, 0.012f, 0.3f }, panel); box({ -0.85f, 0, -0.35f }, { 0.8f, 0.012f, 0.3f }, panel);
            break;
        case Icon::SatStarlink:  // flat bus with one big array
            box({ 0, 0, 0 }, { 0.8f, 0.06f, 0.55f }, body);
            box({ 1.05f, 0, 0 }, { 1.3f, 0.012f, 0.5f }, panel);
            box({ -0.1f, -0.06f, 0 }, { 0.3f, 0.04f, 0.3f }, dark);
            break;
        case Icon::SatNav:
            box({ 0, 0, 0 }, { 0.45f, 0.45f, 0.55f }, gold);
            box({ 0.8f, 0, 0 }, { 0.95f, 0.012f, 0.4f }, panel); box({ -0.8f, 0, 0 }, { 0.95f, 0.012f, 0.4f }, panel);
            tube({ 0, 0.22f, 0 }, { 0, 0.42f, 0 }, 0.14f, 0.05f, body);
            break;
        case Icon::SatWeather:
            box({ 0, 0, 0 }, { 0.5f, 0.5f, 0.55f }, body);
            box({ 0.85f, 0, 0 }, { 1.05f, 0.012f, 0.4f }, panel);
            tube({ -0.25f, 0.15f, 0 }, { -0.62f, 0.15f, 0 }, 0.05f, 0.28f, shade(body, 0.85f));
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
