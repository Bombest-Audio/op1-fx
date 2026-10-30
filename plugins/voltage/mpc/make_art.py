#!/usr/bin/env python3
"""Regenerates every image in mpc/images for the Voltage MPC skin (run from anywhere).

Layers on top of the faceplate (page y = faceplate y + 86):
  bg.png      the hero art with its baked lightning dimmed and its baked needle painted out
  act.png     64 frames 340x120: the lightning between the terminals. frame = xor step * 8 + level step
  needle.png  64 frames 250x150: the red needle, driven by the level step (xor step ignored)
  xor.png fb.png filt.png det.png  33 frames 110x110 each, one per knob
"""
import math
import random
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
OUT = HERE / "images"
SRC = HERE.parent / "design" / "art" / "faceplate-bg-1280x630.png"
OUT.mkdir(exist_ok=True)

BLUE, BLUE_HI = (105, 142, 255), (190, 208, 255)
GREEN, WHITE, ORANGE = (0, 237, 149), (233, 236, 232), (255, 154, 58)
PIVOT = (639.0, 415.0)  # needle pivot on the faceplate
SS = 3  # supersample


def save_strip(name, frames):
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    strip.save(OUT / name, optimize=True)
    return strip.size


def finish(big, size):
    return big.resize(size, Image.LANCZOS)


# ---------------------------------------------------------------- background
def make_bg():
    im = np.array(Image.open(SRC).convert("RGB"))
    h, w = im.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    # erase the baked needle (pivot to tip is about 180 px long): inpaint a band along it
    dx, dy = 769 - PIVOT[0], 296 - PIVOT[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    t = (xx - PIVOT[0]) * ux + (yy - PIVOT[1]) * uy
    d = (xx - PIVOT[0]) * uy - (yy - PIVOT[1]) * ux  # signed distance to the needle line
    k = np.clip((t - 40) / 140.0, 0, 1)
    half_up = 9.5 - 3.5 * k      # upper-left side of the needle
    half_dn = 13.0 - 4.0 * k      # lower-right side, where the baked shadow lies
    band = ((np.where(d < 0, -d < half_up, d < half_dn)) & (t > 40) & (t < 180)).astype(np.float32)
    fimg = im.astype(np.float32)
    lum = fimg.mean(axis=2)
    # paper only: not the needle band, not its red edge, not dark ticks or housing
    wgt = ((cv2.dilate(band, np.ones((5, 5), np.uint8)) == 0) & (lum > 150)
           & ~((fimg[..., 0] > fimg[..., 1] + 40))).astype(np.float32)
    near = (np.abs(d) < 40) & (t > 20) & (t < 205)
    wgt *= near
    num = cv2.GaussianBlur(fimg * wgt[..., None], (0, 0), 10)
    den = cv2.GaussianBlur(wgt, (0, 0), 10)[..., None]
    fill = num / np.maximum(den, 1e-4)
    grain = np.random.RandomState(3).normal(0, 3.0, (h, w, 1))
    soft = cv2.GaussianBlur(band, (0, 0), 2.2)[..., None]
    im = np.clip(fimg * (1 - soft) + (fill + grain) * soft, 0, 255).astype(np.uint8)
    # dim the baked lightning above the housing so the animated bolt is the only one
    f = im.astype(np.float32)
    box = ((xx > 470) & (xx < 815) & (yy > 15) & (yy < 196)).astype(np.float32)
    for x0, x1 in ((476, 530), (750, 808)):  # keep the two copper terminals
        box[(xx >= x0) & (xx <= x1) & (yy >= 123)] = 0
    box = cv2.GaussianBlur(box, (0, 0), 3.0)
    f *= (1 - 0.9 * box)[..., None]
    Image.fromarray(np.clip(f, 0, 255).astype(np.uint8)).save(OUT / "bg.png", optimize=True)


# ---------------------------------------------------------------- lightning
AW, AH = 340, 120
AX0, AY0 = 470, 30  # frame origin on the faceplate
A_PT, B_PT = (517 - AX0, 122 - AY0), (767 - AX0, 121 - AY0)


def bolt(rnd, a, b, amp, depth=7):
    pts = [a, b]
    for _ in range(depth):
        nxt = [pts[0]]
        for p, q in zip(pts, pts[1:]):
            mx, my = (p[0] + q[0]) / 2, (p[1] + q[1]) / 2
            seg = math.hypot(q[0] - p[0], q[1] - p[1])
            nx, ny = -(q[1] - p[1]) / (seg or 1), (q[0] - p[0]) / (seg or 1)
            o = rnd.uniform(-1, 1) * amp * seg * 0.5
            nxt += [(mx + nx * o, my + ny * o), q]
        pts = nxt
    return pts


def lightning_frame(ks, ls):
    img = Image.new("RGBA", (AW * SS, AH * SS), (0, 0, 0, 0))
    if ls == 0:
        return finish(img, (AW, AH))
    lv = ls / 7.0
    rnd = random.Random(ks * 97 + ls * 13 + 5)
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    core = Image.new("RGBA", img.size, (0, 0, 0, 0))
    gd, cd = ImageDraw.Draw(glow), ImageDraw.Draw(core)
    amp = 0.35 + 0.35 * lv
    main = bolt(rnd, A_PT, B_PT, amp, 6)
    paths = [(main, 1.0)]
    for _ in range(1 + ks):  # more branches as XOR goes up
        i = rnd.randrange(len(main) // 6, len(main) - len(main) // 6)
        s = main[i]
        e = (s[0] + rnd.uniform(-45, 45), s[1] + rnd.uniform(-55, -8))
        paths.append((bolt(rnd, s, e, 0.6, 4), 0.55))
    ga = int(255 * (0.18 + 0.55 * lv))
    ca = int(255 * (0.35 + 0.65 * lv))
    for pts, wgt in paths:
        sp = [(x * SS, y * SS) for x, y in pts]
        gd.line(sp, fill=BLUE + (int(ga * wgt),), width=int((4 + 6 * lv) * SS * wgt), joint="curve")
        cd.line(sp, fill=BLUE_HI + (int(ca * wgt),), width=max(1, int((1.0 + 1.6 * lv) * SS * wgt)), joint="curve")
    glow = glow.filter(ImageFilter.GaussianBlur(5 * SS))
    glow2 = glow.filter(ImageFilter.GaussianBlur(2 * SS))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, glow2)
    img = Image.alpha_composite(img, core)
    return finish(img, (AW, AH))


# ---------------------------------------------------------------- needle
NW, NH = 250, 150
NX0, NY0 = 639 - NW // 2, 235  # frame origin on the faceplate
RED = (214, 32, 26)


def needle_frame(ls):
    lv = ls / 7.0
    ang = math.radians(-42 + 84 * lv)  # from vertical, left of zero is negative
    px, py = (PIVOT[0] - NX0) * SS, (PIVOT[1] - NY0) * SS
    ux, uy = math.sin(ang), -math.cos(ang)
    nx, ny = -uy, ux
    r0, r1 = 46 * SS, 176 * SS
    w0, w1 = 4.2 * SS, 1.0 * SS
    poly = [
        (px + ux * r0 + nx * w0, py + uy * r0 + ny * w0),
        (px + ux * r1 + nx * w1, py + uy * r1 + ny * w1),
        (px + ux * r1 - nx * w1, py + uy * r1 - ny * w1),
        (px + ux * r0 - nx * w0, py + uy * r0 - ny * w0),
    ]
    size = (NW * SS, NH * SS)
    sh = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).polygon([(x + 5 * SS, y + 6 * SS) for x, y in poly], fill=(20, 10, 0, 110))
    sh = sh.filter(ImageFilter.GaussianBlur(3 * SS))
    img = Image.alpha_composite(Image.new("RGBA", size, (0, 0, 0, 0)), sh)
    nd = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(nd)
    d.polygon(poly, fill=RED + (255,))
    d.line([(px + ux * r0 + nx * w0 * 0.4, py + uy * r0 + ny * w0 * 0.4),
            (px + ux * r1, py + uy * r1)], fill=(255, 96, 80, 200), width=max(1, SS))
    img = Image.alpha_composite(img, nd)
    return finish(img, (NW, NH))


# ---------------------------------------------------------------- knob elements
S = 110


def canvas():
    return Image.new("RGBA", (S * SS, S * SS), (0, 0, 0, 0))


def glowline(img, pts, col, width, ga=0.45):
    g = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(g).line([(x * SS, y * SS) for x, y in pts], fill=col + (int(255 * ga),), width=int(width * 3 * SS), joint="curve")
    g = g.filter(ImageFilter.GaussianBlur(3 * SS))
    img = Image.alpha_composite(img, g)
    c = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(c).line([(x * SS, y * SS) for x, y in pts], fill=col + (255,), width=max(1, int(width * SS)), joint="curve")
    return Image.alpha_composite(img, c)


def dot(img, x, y, r, col, a=255):
    d = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(d).ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], fill=col + (a,))
    return Image.alpha_composite(img, d)


def ring(img, x, y, r, col, a, width=1.4):
    d = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(d).ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], outline=col + (int(a),), width=max(1, int(width * SS)))
    return Image.alpha_composite(img, d)


def xor_frame(v):
    # a rectified sine morphing into an XOR of two square-ish gates (blue = XOR knob)
    img = canvas()
    pts = []
    for i in range(101):
        t = i / 100.0
        x = 5 + 100 * t
        a = abs(math.sin(2 * math.pi * 1.5 * t))
        s1 = 1.0 if math.sin(2 * math.pi * 1.5 * t) > 0 else 0.0
        s2 = 1.0 if math.sin(2 * math.pi * 2.5 * t) > 0 else 0.0
        xr = 1.0 if s1 != s2 else 0.0
        y = 0.5 * a + 0.5 * a  # keep the sine amplitude
        val = (1 - v) * a + v * (0.35 * a + 0.65 * xr)
        pts.append((x, 78 - 46 * val))
    img = glowline(img, pts, BLUE, 1.7)
    img = glowline(img, [(5, 86), (105, 86)], (60, 70, 105), 1.0, 0.0)
    img = dot(img, 5 + 100 * v, 86, 3.0, BLUE_HI)
    return finish(img, (S, S))


def fb_frame(v):
    # a terminal that rings back into itself: echo rings grow in number and stay bright with feedback
    img = canvas()
    cx = cy = S / 2
    n = 1 + int(round(v * 3))
    for k in range(1, n + 1):
        a = 255 * (0.9 * (0.25 + 0.75 * v)) ** k + 30
        img = ring(img, cx, cy, 9 + 9 * k, GREEN, min(255, a * 0.8))
    img = dot(img, cx, cy, 8, GREEN)
    img = glowline(img, [(cx, cy - 5), (cx, cy + 5)], (7, 12, 10), 1.4, 0.0)
    img = glowline(img, [(cx - 5, cy), (cx + 5, cy)], (7, 12, 10), 1.4, 0.0)
    return finish(img, (S, S))


def filt_frame(v):
    # low-pass curve, the corner slides right as the cutoff opens
    img = canvas()
    cut = 28 + 62 * v
    pts = []
    for i in range(101):
        x = 5 + 100 * i / 100.0
        r = 1.0 / math.sqrt(1 + ((x - 5) / cut) ** 6)
        res = 0.12 * math.exp(-(((x - 5) - cut) / 6.0) ** 2)
        pts.append((x, 90 - 52 * (r + res)))
    img = glowline(img, pts, WHITE, 1.7, 0.30)
    img = glowline(img, [(5, 96), (105, 96)], (70, 74, 72), 1.0, 0.0)
    img = dot(img, 5 + cut, 96, 3.0, WHITE)
    return finish(img, (S, S))


def det_frame(v):
    # three voices on a pitch track, the outer two drift apart as detune rises
    img = canvas()
    cy = S / 2
    img = glowline(img, [(5, cy), (105, cy)], (70, 52, 36), 1.0, 0.0)
    off = 3 + 47 * v
    for sgn in (-1, 1):
        for k in range(1, 4):
            img = dot(img, 55 + sgn * off * k / 3, cy, 3.0, ORANGE, int(70 / k))
        img = dot(img, 55 + sgn * off, cy, 5.5, ORANGE)
    img = dot(img, 55, cy, 6.5, (255, 214, 170))
    return finish(img, (S, S))


def main():
    make_bg()
    sizes = {}
    sizes["act.png"] = save_strip("act.png", [lightning_frame(f // 8, f % 8) for f in range(64)])
    sizes["needle.png"] = save_strip("needle.png", [needle_frame(f % 8) for f in range(64)])
    for name, fn in (("xor.png", xor_frame), ("fb.png", fb_frame), ("filt.png", filt_frame), ("det.png", det_frame)):
        sizes[name] = save_strip(name, [fn(i / 32.0) for i in range(33)])
    for k, v in sizes.items():
        print(k, v)


if __name__ == "__main__":
    main()
