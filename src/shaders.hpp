#pragma once

inline const char* VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec2 uv; out vec3 nrm; out vec3 wpos;
void main(){
    uv = vertexTexCoord;
    nrm = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    wpos = vec3(matModel * vec4(vertexPosition, 1.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

inline const char* FS_EARTH = R"(#version 330
in vec2 uv; in vec3 nrm; in vec3 wpos;
uniform sampler2D texture0; uniform vec3 sunDir; uniform vec3 viewPos; uniform vec3 sunObj;
uniform float cloudT; uniform float cloudAmt; uniform float specAmt;
uniform sampler2D texture1; uniform float nightAmt;
out vec4 finalColor;
float hash(vec3 p){ p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3)); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float noise(vec3 x){
    vec3 i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1,0,0)), f.x), mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x), mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p){ float a = 0.5, s = 0.0; for (int i = 0; i < 5; i++) { s += a * noise(p); p = p * 2.03 + vec3(1.7, 9.2, 3.1); a *= 0.5; } return s; }
float clouds(vec3 sp){ vec3 q = sp * 6.5 + vec3(cloudT * 2.0, 0.0, 0.0); float c = fbm(q + fbm(q * 0.7) * 0.7); return smoothstep(0.52, 0.80, c); }
void main(){
    vec3 N = normalize(nrm), V = normalize(viewPos - wpos);
    float d = dot(N, sunDir);
    float day = smoothstep(-0.12, 0.25, d);
    vec3 tex = texture(texture0, uv).rgb;
    float water = smoothstep(0.015, 0.07, tex.b - max(tex.r * 1.05, tex.g * 0.95));
    float lum = dot(tex, vec3(0.299, 0.587, 0.114));
    tex = mix(vec3(lum), tex, 1.18);
    float lon = (uv.x - 0.5) * 6.2831853, lat = (0.5 - uv.y) * 3.1415927;
    vec3 sp = vec3(cos(lat) * cos(lon), sin(lat), -cos(lat) * sin(lon));
    float cl = 0.0, shadow = 0.0;
    if (cloudAmt > 0.0) {
        cl = clouds(sp) * cloudAmt;
        shadow = clouds(normalize(sp + sunObj * 0.018)) * cloudAmt;
    }
    float diff = max(d, 0.0);
    vec3 lit = tex * (0.045 + diff * 1.30) * (1.0 - 0.38 * shadow);
    vec3 H = normalize(sunDir + V);
    float glint = pow(max(dot(N, H), 0.0), 260.0) * specAmt * water * (1.0 - cl);
    lit += vec3(1.0, 0.93, 0.80) * glint * 0.85 * day;
    float ll = dot(texture(texture1, uv).rgb, vec3(0.30, 0.59, 0.11));
    vec3 night = tex * vec3(0.075, 0.095, 0.16) + vec3(1.0, 0.74, 0.42) * pow(ll, 0.85) * 2.1 * nightAmt * (1.0 - cl * 0.9);
    vec3 col = mix(night, lit, day);
    vec3 cloudCol = mix(vec3(0.045, 0.055, 0.085), vec3(0.20 + diff * 0.95), day);
    col = mix(col, cloudCol, cl * 0.93);
    col += vec3(0.35, 0.12, 0.02) * exp(-pow(d / 0.12, 2.0)) * 0.25;
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    col += vec3(0.20, 0.45, 1.0) * rim * (0.15 + 0.85 * day) * 0.6;
    finalColor = vec4(col, 1.0);
})";

inline const char* FS_ATMO = R"(#version 330
in vec2 uv; in vec3 nrm; in vec3 wpos;
uniform vec3 sunDir; uniform vec3 viewPos;
out vec4 finalColor;
void main(){
    vec3 N = normalize(nrm), V = normalize(viewPos - wpos);
    float ndv = max(dot(N, V), 0.0);
    float rim = pow(1.0 - ndv, 2.2);
    float s = dot(N, sunDir);
    float lit = smoothstep(-0.35, 0.35, s);
    float sunset = exp(-pow(s / 0.20, 2.0));
    vec3 col = mix(vec3(0.25, 0.55, 1.0), vec3(1.0, 0.48, 0.20), sunset * 0.6) * rim * (0.08 + 0.92 * lit);
    col += vec3(0.55, 0.78, 1.0) * pow(1.0 - ndv, 6.0) * 0.40 * lit;
    finalColor = vec4(col, 1.0);
})";

inline const char* VS2DC = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv; out vec4 fragColor;
void main(){ uv = vertexTexCoord; fragColor = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); })";

inline const char* FS_TILE = R"(#version 330
in vec2 uv; in vec4 fragColor;
uniform sampler2D texture0; uniform float dark;
out vec4 finalColor;
void main(){
    vec4 t = texture(texture0, uv);
    vec3 c = t.rgb;
    if (dark > 0.5) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        vec3 chroma = c - vec3(l);
        float dl = 0.055 + 0.82 * pow(1.0 - l, 1.1);
        c = vec3(dl) * vec3(0.95, 0.98, 1.06) + chroma * 0.85;
    }
    finalColor = vec4(c, t.a) * fragColor;
})";

inline const char* FS_MOON = R"(#version 330
in vec2 uv; in vec3 nrm; in vec3 wpos;
uniform vec3 sunDir;
out vec4 finalColor;
void main(){
    vec3 N = normalize(nrm);
    float d = max(dot(N, sunDir), 0.0);
    float m = 0.5 + 0.5 * sin(uv.x * 23.0) * sin(uv.y * 13.0 + 1.3) * sin(uv.x * 7.0 + uv.y * 5.0);
    vec3 base = mix(vec3(0.40), vec3(0.66, 0.63, 0.60), m);
    finalColor = vec4(base * (0.015 + d * 1.25), 1.0);
})";

inline const char* VS2D = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv;
void main(){ uv = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); })";

inline const char* FS_MERC = R"(#version 330
in vec2 uv;
uniform sampler2D texture0; uniform sampler2D texture1; uniform float nightAmt; uniform vec3 sunEcef; uniform vec2 center; uniform vec2 span;
out vec4 finalColor;
void main(){
    float u = center.x + (uv.x - 0.5) * span.x;
    float v = center.y + (uv.y - 0.5) * span.y;
    if (v < 0.0 || v > 1.0) { finalColor = vec4(0.01, 0.012, 0.03, 1.0); return; }
    float lon = (u - 0.5) * 6.2831853;
    float lat = atan(sinh(3.1415927 * (1.0 - 2.0 * v)));
    vec2 g = vec2(u, 0.5 - lat / 3.1415927);
    vec3 tex = textureGrad(texture0, vec2(fract(u), g.y), dFdx(g), dFdy(g)).rgb;
    vec3 n = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
    float d = dot(n, sunEcef);
    float day = smoothstep(-0.10, 0.10, d);
    vec3 lit = tex * (0.55 + 0.45 * clamp(d * 1.6, 0.0, 1.0));
    float ll = dot(textureGrad(texture1, vec2(fract(u), g.y), dFdx(g), dFdy(g)).rgb, vec3(0.30, 0.59, 0.11));
    vec3 night = tex * vec3(0.09, 0.11, 0.19) + vec3(1.0, 0.74, 0.42) * pow(ll, 0.85) * 1.9 * nightAmt;
    vec3 col = mix(night, lit, day);
    col += vec3(0.35, 0.12, 0.02) * exp(-pow(d / 0.08, 2.0)) * 0.20;
    finalColor = vec4(col, 1.0);
})";
