#pragma once
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace etex {

inline float hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u ^ (uint32_t)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}
inline float fade(float t) { return t * t * (3.0f - 2.0f * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float vnoise(float x, float y, float z) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float fx = fade(x - xi), fy = fade(y - yi), fz = fade(z - zi);
    float c[2][2][2];
    for (int dx = 0; dx < 2; ++dx) for (int dy = 0; dy < 2; ++dy) for (int dz = 0; dz < 2; ++dz)
        c[dx][dy][dz] = hash3(xi + dx, yi + dy, zi + dz);
    float x00 = lerpf(c[0][0][0], c[1][0][0], fx), x10 = lerpf(c[0][1][0], c[1][1][0], fx);
    float x01 = lerpf(c[0][0][1], c[1][0][1], fx), x11 = lerpf(c[0][1][1], c[1][1][1], fx);
    return lerpf(lerpf(x00, x10, fy), lerpf(x01, x11, fy), fz);
}
inline float fbm(float x, float y, float z, int oct) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < oct; ++i) {
        sum += amp * vnoise(x, y, z); norm += amp;
        x *= 2.07f; y *= 2.07f; z *= 2.07f; amp *= 0.5f;
    }
    return sum / norm;
}
inline Color mix(Color a, Color b, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return { (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
             (unsigned char)(a.b + (b.b - a.b) * t), 255 };
}

inline Image generate(int w, int h) {
    Image img = GenImageColor(w, h, BLACK);
    Color* px = (Color*)img.data;
    const Color deep{8, 25, 70, 255}, shallow{25, 90, 150, 255};
    const Color sand{194, 178, 128, 255}, grass{60, 120, 50, 255}, forest{30, 80, 40, 255};
    const Color rock{110, 100, 90, 255}, snow{240, 244, 250, 255};
    for (int y = 0; y < h; ++y) {
        float lat = (float)PI * (0.5f - (float)y / h);
        float cl = std::cos(lat), sl = std::sin(lat);
        for (int x = 0; x < w; ++x) {
            float lon = 2.0f * (float)PI * (float)x / w;
            float px3 = cl * std::cos(lon), py3 = sl, pz3 = cl * std::sin(lon);
            float elev = fbm(px3 * 1.7f + 11, py3 * 1.7f + 5, pz3 * 1.7f + 3, 6) - 0.52f;
            float moist = fbm(px3 * 3.0f - 7, py3 * 3.0f, pz3 * 3.0f + 9, 4);
            float alat = std::fabs(lat) * (180.0f / (float)PI);
            Color c;
            if (elev < 0) {
                c = mix(shallow, deep, -elev * 7.0f);
            } else {
                Color low = mix(sand, mix(grass, forest, moist * 1.6f - 0.3f), moist * 2.0f - (alat < 30 ? 0.3f : 0.0f));
                c = mix(low, rock, (elev - 0.12f) * 6.0f);
                c = mix(c, snow, (elev - 0.28f) * 8.0f);
            }
            float ice = (alat - 66.0f - (fbm(px3 * 6, py3 * 6, pz3 * 6, 3) - 0.5f) * 14.0f) / 4.0f;
            if (ice > 0) c = mix(c, snow, ice);
            px[y * w + x] = c;
        }
    }
    return img;
}

}
