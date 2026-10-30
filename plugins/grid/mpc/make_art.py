#!/usr/bin/env python3
"""Regenerates every image of the Grid MPC skin: images/bg.png and the filmstrips.

Run: python3 make_art.py   (from anywhere; writes next to this file under images/)

Layout in page coordinates (the faceplate art line sits at y=86, so these are page pixels):
  lattice   2:1 isometric diamond, 12 x 12 cells, top vertex T=(640,180), cell steps A=(25,12.5), B=(-25,12.5)
  X  (blue)   slice parallel to B at i = 0.5 + 11.5 * x, everything behind it tinted, tick on the top-right edge
  Y  (green)  slice parallel to A at j = 0.5 + 11.5 * y, everything behind it tinted, tick on the top-left edge
              where the two tinted regions overlap is the X by Y mesh the DSP runs
  Z FDB (white) flat diamond rising along a vertical track left of the lattice, with 0..5 echoes trailing under it
  MIX (red)  column of 21 bars right of the lattice, lit from the bottom (the OP-1 screen has the same 21 bars)
  act        centre ripple on the lattice: signal level = how far the lattice is lifted and how bright the rings are,
             lead knob (X) = ring phase. Frame = knob step * 8 + level step.
"""
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent / "images"
S = 2  # supersample

PLATE = (7, 8, 12)
LAT = (56, 38, 73)        # #382649, the OP-1 lattice line colour
LAT_HI = (95, 66, 112)    # #5F4270, its one lit line
BLUE = (105, 142, 255)
GREEN = (0, 237, 149)
WHITE = (173, 173, 255)
WHITE_HI = (223, 217, 255)
RED = (255, 58, 93)
TRACK = (35, 38, 42)

N = 12
T = (640.0, 180.0)
A = (25.0, 12.5)
B = (-25.0, 12.5)


def P(i, j):
    return (T[0] + i * A[0] + j * B[0], T[1] + i * A[1] + j * B[1])


def layer(w, h):
    return Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))


def sc(pts, ox, oy):
    return [((x - ox) * S, (y - oy) * S) for x, y in pts]


def line(d, pts, ox, oy, col, width=1.6, alpha=255):
    d.line(sc(pts, ox, oy), fill=col + (int(alpha),), width=max(1, round(width * S)), joint="curve")


def glow(img, radius, gain=1.0):
    """Soft bloom: blurred copy added under the sharp layer."""
    g = img.filter(ImageFilter.GaussianBlur(radius * S))
    a = g.getchannel("A").point(lambda v: min(255, int(v * gain)))
    g.putalpha(a)
    return Image.alpha_composite(g, img)


def down(img):
    return img.resize((img.width // S, img.height // S), Image.LANCZOS)


def save_strip(frames, path):
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for k, f in enumerate(frames):
        strip.paste(f, (0, k * h))
    assert strip.height <= 12288, (path, strip.height)
    strip.save(path, optimize=True)
    print(f"{path.name}: {len(frames)} frames of {w}x{h}, {strip.width}x{strip.height}")


# ---------------------------------------------------------------- background

def make_bg():
    W, H = 1280, 630
    img = Image.new("RGBA", (W * S, H * S), PLATE + (255,))
    d = ImageDraw.Draw(img)
    oy = 86  # page y of the faceplate top, bg is drawn in faceplate coords
    for k in range(N + 1):
        line(d, [P(k, 0), P(k, N)], 0, oy, LAT, 1.5)
        line(d, [P(0, k), P(N, k)], 0, oy, LAT, 1.5)
    # outline a little brighter, plus the one lit mid line the OP-1 screen has
    line(d, [P(0, 0), P(N, 0), P(N, N), P(0, N), P(0, 0)], 0, oy, LAT_HI, 1.7)
    line(d, [P(N / 2, 0), P(N / 2, N)], 0, oy, LAT_HI, 1.5)
    line(d, [P(0, N / 2), P(N, N / 2)], 0, oy, LAT_HI, 1.5)
    # Z track on the left
    zx = 290
    line(d, [(zx, 195), (zx, 465)], 0, oy, TRACK, 2.0)
    for k in range(11):
        y = 465 - k * 27
        line(d, [(zx + 8, y), (zx + 18, y)], 0, oy, LAT, 1.5)
    # MIX column: 21 dim bars and the short scale ticks on the right of them
    for k in range(21):
        y = MIX_Y0 - 14 * k
        line(d, [(MIX_X0, y), (MIX_X1, y)], 0, oy, LAT, 2.2)
        if k % 2 == 0:
            line(d, [(MIX_X1 + 8, y), (MIX_X1 + 16, y)], 0, oy, LAT, 1.5)
    # a few faint stars so the plate is not dead flat
    import random
    rnd = random.Random(7)
    sd = ImageDraw.Draw(img)
    for _ in range(26):
        x, y = rnd.uniform(40, 1240), rnd.uniform(20, 600)
        if 120 < x < 1160 and 150 < y + oy < 500:
            continue
        r = rnd.choice([0.8, 1.0, 1.3])
        c = rnd.choice([BLUE, GREEN, WHITE])
        sd.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=c + (120,))
    down(img).convert("RGB").save(OUT / "bg.png", optimize=True)
    print("bg.png 1280x630")


MIX_X0, MIX_X1, MIX_Y0 = 985, 1015, 465

# ---------------------------------------------------------------- X / Y slices

CW, CH = 640, 340
COX, COY = 640 - CW // 2, 330 - CH // 2  # page origin of the lattice canvas
FR = 33


def slice_frame(axis, v, col):
    """axis 'x': slice along B at i; axis 'y': slice along A at j."""
    u = 0.5 + (N - 0.5) * v
    im = layer(CW, CH)
    tint = layer(CW, CH)
    td = ImageDraw.Draw(tint)
    if axis == "x":
        quad = [P(0, 0), P(u, 0), P(u, N), P(0, N)]
        edge = [P(u, 0), P(u, N)]
        tick0, tick1 = P(u, 0), (P(u, 0)[0] + 9, P(u, 0)[1] - 9)
    else:
        quad = [P(0, 0), P(N, 0), P(N, u), P(0, u)]
        edge = [P(0, u), P(N, u)]
        tick0, tick1 = P(0, u), (P(0, u)[0] - 9, P(0, u)[1] - 9)
    td.polygon(sc(quad, COX, COY), fill=col + (22,))
    d = ImageDraw.Draw(im)
    # the lattice lines inside the region lift toward the knob colour
    for k in range(N + 1):
        if axis == "x" and k <= u:
            line(d, [P(k, 0), P(k, N)], COX, COY, col, 1.2, 70)
        if axis == "y" and k <= u:
            line(d, [P(0, k), P(N, k)], COX, COY, col, 1.2, 70)
    line(d, edge, COX, COY, col, 2.4, 255)
    line(d, [tick0, tick1], COX, COY, col, 2.4, 255)
    cx, cy = (tick1[0] - COX) * S, (tick1[1] - COY) * S
    r = 3.2 * S
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col + (255,))
    im = glow(Image.alpha_composite(tint, im), 5, 1.1)
    return down(im)


# ---------------------------------------------------------------- Z diamond

ZW, ZH = 80, 310
ZOX, ZOY = 290 - ZW // 2, 330 - ZH // 2


def rhombus(cx, cy, hw=15, hh=7.5):
    return [(cx, cy - hh), (cx + hw, cy), (cx, cy + hh), (cx - hw, cy)]


def z_frame(v):
    im = layer(ZW, ZH)
    d = ImageDraw.Draw(im)
    cy = 465 - v * 270
    echoes = round(v * 5)
    for n in range(echoes, 0, -1):
        y = cy + 19 * n
        if y > 478:
            continue
        a = int(255 * 0.5 ** n)
        d.polygon(sc(rhombus(290, y, 15 - n, 7.5 - n * 0.5), ZOX, ZOY), outline=WHITE + (a,), width=S * 2)
    d.polygon(sc(rhombus(290, cy), ZOX, ZOY), outline=WHITE_HI + (255,), width=round(2.4 * S))
    d.polygon(sc(rhombus(290, cy, 9, 4.5), ZOX, ZOY), fill=WHITE + (110,))
    return down(glow(im, 4, 1.0))


# ---------------------------------------------------------------- MIX bars

MW, MH = 60, 310
MOX, MOY = 1000 - MW // 2, 330 - MH // 2


def mix_frame(k):
    im = layer(MW, MH)
    d = ImageDraw.Draw(im)
    for b in range(k):
        y = MIX_Y0 - 14 * b
        # the top lit bar is brightest, the rest ride a little dimmer like a level column
        a = 255 if b == k - 1 else 205
        line(d, [(MIX_X0, y), (MIX_X1, y)], MOX, MOY, RED, 2.8, a)
    return down(glow(im, 3.5, 1.0))


# ---------------------------------------------------------------- activity ripple

AW, AH = 340, 186
AOX, AOY = 640 - AW // 2, 330 - AH // 2
CENTER = (640.0, 330.0)


def warp(i, j, L, phase):
    x, y = P(i, j)
    di, dj = i - N / 2, j - N / 2
    r = math.hypot(di, dj)
    h = math.cos(1.5 * r - phase) * math.exp(-(r * r) / 16.0)
    return (x, y - 20.0 * L * h)


def act_frame(ks, ls):
    L = ls / 7.0
    if ls == 0:
        return Image.new("RGBA", (AW, AH), (0, 0, 0, 0))
    phase = ks / 8.0 * 2 * math.pi
    im = layer(AW, AH)
    d = ImageDraw.Draw(im)
    # lifted lattice, brightness follows the level
    a_line = int(235 * (0.25 + 0.75 * L))
    steps = 6
    for k in range(N + 1):
        pa, pb = [], []
        for s in range(N * steps + 1):
            t = s / steps
            pa.append(warp(k, t, L, phase))
            pb.append(warp(t, k, L, phase))
        line(d, pa, AOX, AOY, (150, 120, 235), 1.5, a_line)
        line(d, pb, AOX, AOY, (150, 120, 235), 1.5, a_line)
    # 2:1 rings leaving the origin, more of them the louder it is
    for n in range(1 + ls // 3):
        rr = 14 + 34 * (((ks / 8.0) + n / (1 + ls // 3)) % 1.0)
        fade = 1.0 - (rr - 14) / 40.0
        x0, y0 = (CENTER[0] - AOX) * S, (CENTER[1] - AOY) * S
        d.ellipse([x0 - rr * 2 * S, y0 - rr * S, x0 + rr * 2 * S, y0 + rr * S],
                  outline=GREEN + (int(150 * L * fade),), width=round(1.8 * S))
    # soft glow at the origin
    gl = layer(AW, AH)
    gd = ImageDraw.Draw(gl)
    x0, y0 = (CENTER[0] - AOX) * S, (CENTER[1] - AOY) * S
    rg = (10 + 14 * L) * S
    gd.ellipse([x0 - rg * 1.6, y0 - rg * 0.8, x0 + rg * 1.6, y0 + rg * 0.8], fill=BLUE + (int(150 * L),))
    gl = gl.filter(ImageFilter.GaussianBlur(9 * S))
    im = Image.alpha_composite(gl, glow(im, 4, 0.9))
    # feather the whole frame toward its edges so no hard crop shows
    mask = Image.new("L", (AW * S, AH * S), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([8 * S, 4 * S, (AW - 8) * S, (AH - 4) * S], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(22 * S))
    im.putalpha(ImageChops.multiply(im.getchannel("A"), mask))
    return down(im)


def main():
    OUT.mkdir(exist_ok=True)
    make_bg()
    save_strip([slice_frame("x", k / (FR - 1), BLUE) for k in range(FR)], OUT / "x.png")
    save_strip([slice_frame("y", k / (FR - 1), GREEN) for k in range(FR)], OUT / "y.png")
    save_strip([z_frame(k / (FR - 1)) for k in range(FR)], OUT / "zfb.png")
    save_strip([mix_frame(k) for k in range(22)], OUT / "mix.png")
    save_strip([act_frame(ks, ls) for ks in range(8) for ls in range(8)], OUT / "act.png")


if __name__ == "__main__":
    main()
