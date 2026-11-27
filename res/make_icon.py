import math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 1024                                  # master canvas
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # repo root (this file lives in res/)
tex = Image.open(os.path.join(ROOT, "assets", "earth.jpg")).convert("RGB")
tw, th = tex.size
tpx = tex.load()

# ---- background: deep-space rounded square with a soft vertical gradient
bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
grad = Image.new("RGB", (S, S))
gp = grad.load()
for y in range(S):
    t = y / (S - 1)
    c = (int(7 + 16 * t), int(10 + 22 * t), int(26 + 40 * t))
    for x in range(S):
        gp[x, y] = c
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=255)
bg.paste(grad, (0, 0), mask)

# a few stars
import random
random.seed(11)
d = ImageDraw.Draw(bg)
for _ in range(70):
    x, y = random.randint(40, S - 40), random.randint(40, S - 40)
    a = random.randint(60, 200)
    r = random.choice([1, 1, 2, 2, 3])
    d.ellipse([x - r, y - r, x + r, y + r], fill=(200, 215, 255, a))

# ---- globe (orthographic projection of the real Blue Marble texture)
cx, cy, R = S * 0.5, S * 0.52, S * 0.335
lat0, lon0 = math.radians(18.0), math.radians(-55.0)
sl0, cl0 = math.sin(lat0), math.cos(lat0)
light = (-0.55, 0.50, 0.67)
ln = math.sqrt(sum(v * v for v in light)); light = tuple(v / ln for v in light)
globe = Image.new("RGBA", (S, S), (0, 0, 0, 0))
gl = globe.load()
for py in range(int(cy - R) - 2, int(cy + R) + 3):
    for px in range(int(cx - R) - 2, int(cx + R) + 3):
        x = (px + 0.5 - cx) / R
        y = (cy - py - 0.5) / R
        r2 = x * x + y * y
        if r2 > 1.0:
            continue
        z = math.sqrt(1.0 - r2)
        rho = math.sqrt(r2)
        if rho < 1e-9:
            lat, lon = lat0, lon0
        else:
            c = math.asin(min(1.0, rho))
            lat = math.asin(math.cos(c) * sl0 + y * math.sin(c) * cl0 / rho)
            lon = lon0 + math.atan2(x * math.sin(c), rho * cl0 * math.cos(c) - y * sl0 * math.sin(c))
        u = (lon / (2 * math.pi) + 0.5) % 1.0
        v = 0.5 - lat / math.pi
        fx, fy = u * (tw - 1), max(0.0, min(th - 1.001, v * (th - 1)))
        x0, y0 = int(fx), int(fy)
        x1 = (x0 + 1) % tw
        ax, ay = fx - x0, fy - y0
        p00, p10, p01, p11 = tpx[x0, y0], tpx[x1, y0], tpx[x0, y0 + 1], tpx[x1, y0 + 1]
        col = [p00[i] * (1 - ax) * (1 - ay) + p10[i] * ax * (1 - ay) + p01[i] * (1 - ax) * ay + p11[i] * ax * ay for i in range(3)]
        # boost saturation a touch
        lum = 0.299 * col[0] + 0.587 * col[1] + 0.114 * col[2]
        col = [lum + (v_ - lum) * 1.25 for v_ in col]
        # lighting
        dif = max(0.0, x * light[0] + y * light[1] + z * light[2])
        shade = 0.20 + 0.95 * dif
        rim = (1.0 - z) ** 2.6
        col = [col[0] * shade + 40 * rim, col[1] * shade + 110 * rim, col[2] * shade + 235 * rim]
        # anti-aliased edge
        edge = min(1.0, (1.0 - rho) * R * 0.9)
        gl[px, py] = (min(255, int(col[0])), min(255, int(col[1])), min(255, int(col[2])), int(255 * edge))

# ---- atmosphere glow behind the globe
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse([cx - R * 1.07, cy - R * 1.07, cx + R * 1.07, cy + R * 1.07], fill=(70, 150, 255, 140))
glow = glow.filter(ImageFilter.GaussianBlur(S * 0.028))

# ---- orbit ring: tilted ellipse, back half behind the globe, front half over it
def orbit_layer(front):
    lay = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    dd = ImageDraw.Draw(lay)
    rx, ry, tilt = S * 0.455, S * 0.145, math.radians(-24)
    pts = []
    N = 360
    for i in range(N + 1):
        t = 2 * math.pi * i / N
        if (math.sin(t) > 0) != front:
            pts.append(None); continue
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        x = cx + ex * math.cos(tilt) - ey * math.sin(tilt)
        y = cy + ex * math.sin(tilt) + ey * math.cos(tilt)
        pts.append((x, y))
    seg = []
    for p in pts + [None]:
        if p is None:
            if len(seg) > 1:
                dd.line(seg, fill=(255, 196, 80, 255), width=int(S * 0.0105), joint="curve")
            seg = []
        else:
            seg.append(p)
    return lay, (rx, ry, tilt)

back, orb = orbit_layer(False)
front, _ = orbit_layer(True)
rx, ry, tilt = orb
# satellite on the front arc
ts = math.radians(58)
sx = cx + rx * math.cos(ts) * math.cos(tilt) - ry * math.sin(ts) * math.sin(tilt)
sy = cy + rx * math.cos(ts) * math.sin(tilt) + ry * math.sin(ts) * math.cos(tilt)

def soft(layer, blur, boost=1.0):
    b = layer.filter(ImageFilter.GaussianBlur(blur))
    if boost != 1.0:
        a = b.split()[3].point(lambda v: min(255, int(v * boost)))
        b.putalpha(a)
    return b

canvas = bg.copy()
canvas = Image.alpha_composite(canvas, glow)
canvas = Image.alpha_composite(canvas, soft(back, S * 0.010, 1.6))
canvas = Image.alpha_composite(canvas, back)
canvas = Image.alpha_composite(canvas, globe)
canvas = Image.alpha_composite(canvas, soft(front, S * 0.010, 1.6))
canvas = Image.alpha_composite(canvas, front)

# satellite: glowing dot with two little solar panels
sat = Image.new("RGBA", (S, S), (0, 0, 0, 0))
sd = ImageDraw.Draw(sat)
sd.ellipse([sx - S * 0.034, sy - S * 0.034, sx + S * 0.034, sy + S * 0.034], fill=(255, 235, 160, 200))
canvas = Image.alpha_composite(canvas, sat.filter(ImageFilter.GaussianBlur(S * 0.02)))
sat = Image.new("RGBA", (S, S), (0, 0, 0, 0))
sd = ImageDraw.Draw(sat)
ang = tilt + math.radians(20)
def panel(off):
    ux, uy = math.cos(ang), math.sin(ang)
    vx, vy = -uy, ux
    cxp, cyp = sx + ux * off, sy + uy * off
    w, h = S * 0.034, S * 0.016
    return [(cxp + ux * w + vx * h, cyp + uy * w + vy * h), (cxp + ux * w - vx * h, cyp + uy * w - vy * h),
            (cxp - ux * w - vx * h, cyp - uy * w - vy * h), (cxp - ux * w + vx * h, cyp - uy * w + vy * h)]
for off in (-S * 0.052, S * 0.052):
    sd.polygon(panel(off), fill=(120, 190, 255, 255), outline=(220, 240, 255, 255))
sd.ellipse([sx - S * 0.020, sy - S * 0.020, sx + S * 0.020, sy + S * 0.020], fill=(255, 250, 235, 255))
canvas = Image.alpha_composite(canvas, sat)

# subtle border
ImageDraw.Draw(canvas).rounded_rectangle([2, 2, S - 3, S - 3], radius=int(S * 0.22), outline=(255, 255, 255, 38), width=3)

# ---- outputs
os.makedirs(os.path.join(ROOT, "res"), exist_ok=True)
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = {n: canvas.resize((n, n), Image.LANCZOS) for n in sizes}
imgs[256].save(os.path.join(ROOT, "res", "spacemap.ico"), format="ICO", sizes=[(n, n) for n in sizes], append_images=[imgs[n] for n in sizes if n != 256])
imgs[256].save(os.path.join(ROOT, "res", "spacemap_256.png"))
imgs[128].save(os.path.join(ROOT, "res", "spacemap_128.png"))
canvas.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "res", "spacemap_512.png"))

# embed the 128 px PNG for the window icon (no file lookups at run time)
data = open(os.path.join(ROOT, "res", "spacemap_128.png"), "rb").read()
with open(os.path.join(ROOT, "src", "icon_png.hpp"), "w", newline="\n") as f:
    f.write("// Generated from res/spacemap_128.png by res/make_icon.py - window / title-bar icon (PNG).\n#pragma once\nstatic const unsigned char kIconPng[] = {\n")
    for i in range(0, len(data), 24):
        f.write("    " + ",".join("0x%02x" % b for b in data[i:i + 24]) + ",\n")
    f.write("};\n")
print("icon written:", os.path.getsize(os.path.join(ROOT, "res", "spacemap.ico")), "bytes ico;", len(data), "bytes embedded png")
