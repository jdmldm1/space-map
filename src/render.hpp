#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "catalog.hpp"
#include "earth_texture.hpp"
#include "paths.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

inline void drawOrbitPath(const Entity& e, double t, Color c) {
    const int N = 256;
    if (e.orbit.useSgp4) {
        const double per = e.orbit.period();
        Vector3 pv = toRender(e.orbit.positionAt(t));
        for (int i = 1; i <= N; ++i) {
            Vector3 cur = toRender(e.orbit.positionAt(t + per * i / N));
            DrawLine3D(pv, cur, c);
            pv = cur;
        }
        return;
    }
    Vector3 prev = toRender(e.orbit.positionFromMean(0.0, t - e.orbit.epoch));
    for (int i = 1; i <= N; ++i) {
        Vector3 cur = toRender(e.orbit.positionFromMean(2.0 * PI_D * i / N, t - e.orbit.epoch));
        DrawLine3D(prev, cur, c);
        prev = cur;
    }
}

inline bool occludedByBody(Vector3 cam, Vector3 p, float radius) {
    Vector3 d = Vector3Subtract(p, cam);
    float dd = Vector3DotProduct(d, d);
    float t = Clamp(-Vector3DotProduct(cam, d) / dd, 0.0f, 1.0f);
    Vector3 c = Vector3Add(cam, Vector3Scale(d, t));
    return Vector3Length(c) < radius * 0.999f;
}

inline Texture2D loadEarthTexture() {
    for (const char* candidate : { "assets/earth.jpg", "assets/earth.png" }) {
        const std::string path = paths::assetPath(candidate);
        if (!FileExists(path.c_str())) continue;
        Texture2D texture = LoadTexture(path.c_str());
        if (texture.id) {
            TraceLog(LOG_INFO, "Earth texture: %s", path.c_str());
            return texture;
        }
    }
    TraceLog(LOG_INFO, "Generating procedural Earth texture...");
    Image image = etex::generate(2048, 1024);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

inline void saveShot(const std::string& name) {
    rlDrawRenderBatchActive();  // flush batched 2D drawing (the HUD) before reading the framebuffer
    const int w = GetRenderWidth(), h = GetRenderHeight();
    unsigned char* px = (unsigned char*)rlReadScreenPixels(w, h);
    if (!px) return;
    Image img{ px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    ExportImage(img, name.c_str());
    RL_FREE(px);
    TraceLog(LOG_INFO, "Screenshot saved: %s (%dx%d)", name.c_str(), w, h);
}

inline Mesh makeSphere(float radius, int rings, int slices) {
    Mesh m{};
    const int vc = (rings + 1) * (slices + 1);
    m.vertexCount = vc;
    m.triangleCount = rings * slices * 2;
    m.vertices = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(vc * 2 * sizeof(float));
    m.indices = (unsigned short*)MemAlloc(m.triangleCount * 3 * sizeof(unsigned short));
    for (int i = 0; i <= rings; ++i) {
        float v = (float)i / rings, lat = (0.5f - v) * (float)PI;
        for (int j = 0; j <= slices; ++j) {
            float u = (float)j / slices, lon = (u - 0.5f) * 2.0f * (float)PI;
            float nx = std::cos(lat) * std::cos(lon), ny = std::sin(lat), nz = -std::cos(lat) * std::sin(lon);
            int k = i * (slices + 1) + j;
            m.vertices[k * 3] = nx * radius; m.vertices[k * 3 + 1] = ny * radius; m.vertices[k * 3 + 2] = nz * radius;
            m.normals[k * 3] = nx; m.normals[k * 3 + 1] = ny; m.normals[k * 3 + 2] = nz;
            m.texcoords[k * 2] = u; m.texcoords[k * 2 + 1] = v;
        }
    }
    int t = 0;
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            unsigned short a = (unsigned short)(i * (slices + 1) + j), b = (unsigned short)(a + slices + 1);
            m.indices[t++] = a; m.indices[t++] = b; m.indices[t++] = a + 1;
            m.indices[t++] = a + 1; m.indices[t++] = b; m.indices[t++] = b + 1;
        }
    UploadMesh(&m, false);
    return m;
}

inline void drawGroundTrack(const Entity& e, double now, double earthRkm, Color c) {
    const int N = 240;
    const double span = std::min(e.orbit.period(), 1.5 * 86400.0);
    Vector3 prev{}; bool have = false;
    for (int i = 0; i <= N; ++i) {
        const double t = now + span * i / N;
        Vec3d p = e.orbit.positionAt(t);
        const double d = -2.0 * PI_D * 1.00273781191135448 * (t - now) / 86400.0, cd = std::cos(d), sd = std::sin(d);
        Vec3d q{ p.x * cd - p.y * sd, p.x * sd + p.y * cd, p.z };
        const double k = earthRkm * 1.003 / std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        Vector3 cur = toRender({ q.x * k, q.y * k, q.z * k });
        if (have) DrawLine3D(prev, cur, c);
        prev = cur; have = true;
    }
}

struct PointCanvas {
    int w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    std::vector<unsigned> px;
    Texture2D tex{};
    void begin(float W, float H) {
        const int rw = std::max(1, GetRenderWidth()), rh = std::max(1, GetRenderHeight());
        if (rw != w || rh != h) {
            if (tex.id) UnloadTexture(tex);
            w = rw; h = rh;
            Image img = GenImageColor(w, h, BLACK);
            tex = LoadTextureFromImage(img);
            UnloadImage(img);
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
            px.assign((size_t)w * h, 0xFF000000u);
        } else std::fill(px.begin(), px.end(), 0xFF000000u);
        sx = (float)w / W; sy = (float)h / H;
    }
    inline void plot(float x, float y, int r, int g, int b, int size = 1) {
        const int ix = (int)(x * sx), iy = (int)(y * sy);
        const int x0 = ix - size / 2, y0 = iy - size / 2;
        for (int yy = std::max(0, y0); yy < std::min(h, y0 + size); ++yy)
            for (int xx = std::max(0, x0); xx < std::min(w, x0 + size); ++xx) {
                unsigned char* p = (unsigned char*)&px[(size_t)yy * w + xx];
                p[0] = (unsigned char)std::min(255, p[0] + r); p[1] = (unsigned char)std::min(255, p[1] + g); p[2] = (unsigned char)std::min(255, p[2] + b);
            }
    }
    void flush(float W, float H) {
        UpdateTexture(tex, px.data());
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(tex, { 0, 0, (float)w, (float)h }, { 0, 0, W, H }, { 0, 0 }, 0.0f, WHITE);
        EndBlendMode();
    }
};

inline Color altColor(double ft) {
    static const double s[][4] = { { 0, 255, 90, 60 }, { 5000, 255, 170, 40 }, { 10000, 235, 230, 60 }, { 20000, 90, 230, 110 },
                                   { 30000, 70, 200, 240 }, { 40000, 120, 130, 255 }, { 46000, 200, 110, 255 } };
    const int n = 7;
    if (ft <= s[0][0]) return { 255, 90, 60, 255 };
    for (int i = 1; i < n; ++i)
        if (ft <= s[i][0]) {
            const double t = (ft - s[i - 1][0]) / (s[i][0] - s[i - 1][0]);
            return { (unsigned char)(s[i - 1][1] + t * (s[i][1] - s[i - 1][1])), (unsigned char)(s[i - 1][2] + t * (s[i][2] - s[i - 1][2])),
                     (unsigned char)(s[i - 1][3] + t * (s[i][3] - s[i - 1][3])), 255 };
        }
    return { 200, 110, 255, 255 };
}
