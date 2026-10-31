#!/usr/bin/env python3
"""Regenerates every image in mpc/images/ for the Nitro MPC skin. Run: python3 make_art.py

Animated elements (all RGBA filmstrips, vertical stacks of frames, transparent edges):
  freq.png   33 frames  blue needle on a half dial (FREQ)
  res.png    33 frames  green ten-bar stack, bars light up with RES
  shape.png  33 frames  white filter response curve, morphs LP 24 -> THRU -> HP 24 (SHAPE)
  drive.png  33 frames  red ten-bar stack, bars light up with DRIVE
  mix.png    33 frames  twelve lilac ticks fill from the left, dry to wet (MIX), dim slots always drawn
  act.png    64 frames  valve glow, frame = freq step * 8 + level step (written by the engine)
  mist.png   64 frames  violet mist at the bottle foot, same frame number as act
Colours come from design/BRIEF.md. Drawn in my own style, nothing is copied from the OP-1 art.
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images")
SS = 3  # supersampling for anti-aliasing

BLUE = (0x69, 0x8E, 0xFF)
MINT = (0x00, 0xED, 0x95)
LILAC = (0xDF, 0xD9, 0xFF)
RED = (0xFF, 0x3A, 0x5D)
VIOLET = (0x8A, 0x55, 0xFF)
TRACK = (0x4A, 0x50, 0x58)


def clamp(x, a=0.0, b=1.0):
    return a if x < a else (b if x > b else x)


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def finish(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def rgba(c, a):
    return (c[0], c[1], c[2], int(round(255 * clamp(a))))


def glow(img, radius, gain=1.0):
    """Additive blur of the layer under itself, for a soft neon bloom."""
    b = img.filter(ImageFilter.GaussianBlur(radius * SS))
    a = np.asarray(b).astype(np.float32)
    a[..., 3] *= gain
    return Image.alpha_composite(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)), img)


def stack(frames, name):
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    assert strip.height <= 12288, (name, strip.height)
    strip.save(os.path.join(OUT, name), optimize=True)
    print(f"{name}: {w}x{h} x {len(frames)} = {strip.size}")


def line(d, p0, p1, w, col):
    d.line([(p0[0] * SS, p0[1] * SS), (p1[0] * SS, p1[1] * SS)], fill=col, width=int(w * SS))
    r = w * SS / 2
    for p in (p0, p1):
        d.ellipse([p[0] * SS - r, p[1] * SS - r, p[0] * SS + r, p[1] * SS + r], fill=col)


# ---------------------------------------------------------------- FREQ: half dial, needle sweeps left to right
def freq_frame(t, W=200, H=130):
    img = canvas(W, H)
    d = ImageDraw.Draw(img)
    cx, cy, R = W / 2, H - 20, 82
    # dim track arc and ticks (always visible, so the dial reads at rest)
    d.arc([(cx - R) * SS, (cy - R) * SS, (cx + R) * SS, (cy + R) * SS], 180, 360, fill=rgba(TRACK, 0.55), width=2 * SS)
    for i in range(9):
        a = math.pi * (1 - i / 8)
        major = i % 2 == 0
        r0, r1 = R - (12 if major else 7), R + 6
        line(d, (cx + r0 * math.cos(a), cy - r0 * math.sin(a)), (cx + r1 * math.cos(a), cy - r1 * math.sin(a)),
             1.6 if major else 1.0, rgba(TRACK, 0.9 if major else 0.6))
    ang = math.pi * (1 - t)
    # lit arc from the left stop up to the needle
    lit = canvas(W, H)
    ld = ImageDraw.Draw(lit)
    if t > 0.005:
        ld.arc([(cx - R) * SS, (cy - R) * SS, (cx + R) * SS, (cy + R) * SS], 180, 180 + 180 * t,
               fill=rgba(BLUE, 0.85), width=int(2.4 * SS))
    nx, ny = cx + (R - 6) * math.cos(ang), cy - (R - 6) * math.sin(ang)
    line(ld, (cx, cy), (nx, ny), 2.4, rgba(BLUE, 1.0))
    r = 7 * SS
    ld.ellipse([cx * SS - r, cy * SS - r, cx * SS + r, cy * SS + r], fill=rgba(BLUE, 1.0))
    lit = glow(lit, 3.5, 1.3)
    img = Image.alpha_composite(img, lit)
    return finish(img, W, H)


# ---------------------------------------------------------------- RES / DRIVE: ten-bar stack
def bars_frame(t, col, W=170, H=180, taper=False):
    img = canvas(W, H)
    d = ImageDraw.Draw(img)
    n, bh, gap = 10, 7, 10.4
    y0 = H - 14
    lit = canvas(W, H)
    ld = ImageDraw.Draw(lit)
    level = t * n
    for i in range(n):
        y = y0 - i * gap
        # taper: bars widen toward the top for DRIVE (a flare), RES keeps them equal
        wbar = 120 + (i * 3.2 if taper else 0)
        x0, x1 = (W - wbar) / 2, (W + wbar) / 2
        d.rounded_rectangle([x0 * SS, (y - bh / 2) * SS, x1 * SS, (y + bh / 2) * SS], radius=2 * SS,
                            fill=rgba(TRACK, 0.5))
        b = clamp(level - i)
        if b > 0:
            ld.rounded_rectangle([x0 * SS, (y - bh / 2) * SS, x1 * SS, (y + bh / 2) * SS], radius=2 * SS,
                                 fill=rgba(col, 0.25 + 0.75 * b))
    lit = glow(lit, 4, 0.9)
    return finish(Image.alpha_composite(img, lit), W, H)


# ---------------------------------------------------------------- SHAPE: real response of the Nitro shape table
SHAPES = [
    [0, 0, 0, 0, 1], [0, 0, 0, 1, 0], [0, 0, 1, 0, 0], [0, 1, 0, 0, 0], [1, 0, 0, 0, 0], [1, -3, 6, -4, 0],
    [1, -2, 2, 0, 0], [0, 1, -1, 0, 0], [1, -1, 0, 0, 0], [1, -2, 1, 0, 0], [1, -3, 3, -1, 0], [1, -4, 6, -4, 1],
]


def response_db(mix, f):
    """Magnitude in dB of mix . [1, H, H^2, H^3, H^4] with H = 1/(1 + j f), f normalised to the cutoff."""
    h = 1.0 / (1.0 + 1j * f)
    v = sum(m * h ** k for k, m in enumerate(mix))
    return 20.0 * math.log10(max(abs(v), 1e-3))


def shape_frame(t, W=240, H=130):
    pos = t * 11.0
    r = min(int(pos), 10)
    fr = pos - r
    mix = [SHAPES[r][k] * (1 - fr) + SHAPES[r + 1][k] * fr for k in range(5)]
    img = canvas(W, H)
    d = ImageDraw.Draw(img)
    x0, x1, yb, ytop = 14, W - 14, H - 14, 16
    # dim frame: baseline and unity line
    def ypos(db):
        return yb - (clamp((db + 36.0) / 42.0)) * (yb - ytop)
    line(d, (x0, yb), (x1, yb), 1.2, rgba(TRACK, 0.5))
    line(d, (x0, ypos(0)), (x1, ypos(0)), 1.0, rgba(TRACK, 0.35))
    line(d, ((x0 + x1) / 2, ytop - 2), ((x0 + x1) / 2, yb), 1.0, rgba(TRACK, 0.35))  # cutoff marker
    pts = []
    for i in range(121):
        u = i / 120.0
        f = 10 ** (-1.5 + 3.0 * u)  # 0.03 .. 31 x cutoff
        pts.append((x0 + u * (x1 - x0), ypos(response_db(mix, f))))
    lit = canvas(W, H)
    ld = ImageDraw.Draw(lit)
    # soft fill under the curve
    poly = [(p[0] * SS, p[1] * SS) for p in pts] + [(x1 * SS, yb * SS), (x0 * SS, yb * SS)]
    d.polygon(poly, fill=rgba(LILAC, 0.06))
    for a, b in zip(pts, pts[1:]):
        line(ld, a, b, 2.4, rgba(LILAC, 1.0))
    lit = glow(lit, 3.0, 0.9)
    return finish(Image.alpha_composite(img, lit), W, H)


# ---------------------------------------------------------------- ACT: valve glow, level axis pulses, freq axis balances blue/red
def edge_fade(W, H, m=44):
    """Window that goes to zero at the frame edge, so a soft glow never shows a hard box."""
    def ramp(n):
        r = np.minimum(np.arange(n), np.arange(n)[::-1]).astype(np.float32) / m
        r = np.clip(r, 0, 1)
        return r * r * (3 - 2 * r)
    return ramp(H)[:, None] * ramp(W)[None, :]


def act_frame(ks, ls, W=420, H=190):
    lv = ls / 7.0
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if ls == 0:
        return img
    cx, cy = 202, 100  # valve, in frame pixels (page 612, 195 with the meter at cx=612 cy=190)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    bal = ks / 7.0  # 0 = red heavy, 1 = blue heavy
    out = np.zeros((H, W, 4), np.float32)

    def lobe(ox, oy, rad, col, w):
        g = np.exp(-(((xx - cx - ox) ** 2 + (yy - cy - oy) ** 2) / (2 * rad * rad)))
        for c in range(3):
            out[..., c] += col[c] * g * w
        out[..., 3] += 255 * g * w

    lobe(-14, -4, 38 + 22 * lv, BLUE, 0.20 * lv * (0.4 + 0.9 * bal))
    lobe(14, -4, 38 + 22 * lv, RED, 0.20 * lv * (0.4 + 0.9 * (1 - bal)))
    lobe(0, 6, 22 + 10 * lv, (255, 190, 110), 0.16 * lv)  # warm brass core
    # a thin ring that grows with level, the pulse
    rr = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    ring_r = 26 + 40 * lv
    ring = np.exp(-((rr - ring_r) ** 2) / (2 * 2.2 ** 2)) * 0.22 * lv * (1 - 0.35 * lv)
    mix = np.array([BLUE[i] * bal + RED[i] * (1 - bal) for i in range(3)], np.float32)
    for c in range(3):
        out[..., c] += mix[c] * ring
    out[..., 3] += 255 * ring
    a = np.clip(out[..., 3], 0, 255)
    rgb = np.where(a[..., None] > 0, out[..., :3] / np.maximum(out[..., 3:4], 1e-3) * 255.0, 0)
    a = np.minimum(a, 255 * 0.38) * edge_fade(W, H)
    arr = np.dstack([np.clip(rgb, 0, 255), a]).astype(np.uint8)
    return Image.fromarray(arr, "RGBA")


def mist_frame(ks, ls, W=560, H=190):
    lv = ls / 7.0
    if ls == 0:
        return Image.new("RGBA", (W, H), (0, 0, 0, 0))
    rng = np.random.RandomState(7)
    blobs = [(rng.uniform(-170, 170), rng.uniform(-30, 45), rng.uniform(34, 70), rng.uniform(0, 6.28)) for _ in range(14)]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    cx, cy = W / 2, 112
    acc = np.zeros((H, W), np.float32)
    sway = (ks / 7.0 - 0.5) * 30
    for ox, oy, rad, ph in blobs:
        bx = cx + ox + sway * math.sin(ph) + 6 * math.sin(ph + ks)
        by = cy + oy - 10 * lv * (0.5 + 0.5 * math.sin(ph))
        s = rad * (0.85 + 0.35 * lv)
        acc += np.exp(-(((xx - bx) ** 2) / (2 * (s * 1.5) ** 2) + ((yy - by) ** 2) / (2 * (s * 0.8) ** 2)))
    acc = np.clip(acc / 3.0, 0, 1) * lv
    alpha = np.minimum(acc * 255 * 0.30, 255 * 0.30) * edge_fade(W, H, 60)
    arr = np.zeros((H, W, 4), np.uint8)
    arr[..., 0], arr[..., 1], arr[..., 2] = VIOLET
    arr[..., 3] = alpha.astype(np.uint8)
    return Image.fromarray(arr, "RGBA")


def mix_frame(t, W=120, H=22, n=12, pitch=10, tw=6, th=14):
    img = canvas(W, H)
    d = ImageDraw.Draw(img)
    c = t * n
    for k in range(n):
        x = 5 + k * pitch
        y = (H - th) / 2
        box = [x * SS, y * SS, (x + tw) * SS, (y + th) * SS]
        d.rectangle(box, outline=rgba(LILAC, 0.28), width=SS)
        a = clamp(c - k)
        if a > 0:
            d.rectangle(box, fill=rgba(LILAC, 0.27 + 0.73 * a))
    return finish(img, W, H)


def main():
    os.makedirs(OUT, exist_ok=True)
    n = 33
    stack([freq_frame(i / (n - 1)) for i in range(n)], "freq.png")
    stack([bars_frame(i / (n - 1), MINT) for i in range(n)], "res.png")
    stack([bars_frame(i / (n - 1), RED, taper=True) for i in range(n)], "drive.png")
    stack([shape_frame(i / (n - 1)) for i in range(n)], "shape.png")
    stack([mix_frame(i / (n - 1)) for i in range(n)], "mix.png")
    stack([act_frame(f // 8, f % 8) for f in range(64)], "act.png")
    stack([mist_frame(f // 8, f % 8) for f in range(64)], "mist.png")


if __name__ == "__main__":
    main()
