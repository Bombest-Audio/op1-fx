#!/usr/bin/env python3
"""Regenerates every animated image of the Spring MPC skin (images/*.png).

Knob strips (33 frames, frame i = knob value i/32, top to bottom):
  tone.png   blue      low-pass curve, the corner slides right as TONE opens
  turns.png  green     a spring coil that gains turns as TURNS rises
  damp.png   lavender  decaying echo comb, the tail shortens as DAMP rises
  level.png  orange    the OP-1 dashed column, the two dots pull apart as LEVEL rises
Signal strip (64 frames, frame = tone step * 8 + level step, both 0..7):
  act.png    mint glow travelling along both coils, longer and brighter with level, a little bluer with TONE

Layout: images/*.png are placed by layout.conf. Run: python3 make_art.py
"""
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
SS = 4  # supersampling factor for the line elements

BLUE = (105, 142, 255)
GREEN = (46, 240, 160)
LAV = (205, 184, 255)
ORANGE = (255, 138, 77)
TRACK = (35, 38, 42)

KNOB_FRAMES = 33
CELL_W, CELL_H = 160, 110


def save_strip(frames, name):
    w, h = frames[0].size
    assert h * len(frames) <= 12288, f"{name}: strip too tall"
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    os.makedirs(OUT, exist_ok=True)
    strip.save(os.path.join(OUT, name), optimize=True)
    print(f"{name}: {strip.size[0]}x{strip.size[1]} ({len(frames)} frames)")


class Canvas:
    """Draw at SS times the size, downsample with a box filter on save."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def line(self, pts, color, width, alpha=255):
        p = [(x * SS, y * SS) for x, y in pts]
        self.d.line(p, fill=color + (alpha,), width=max(1, int(width * SS)), joint="curve")
        r = width * SS / 2
        for x, y in (p[0], p[-1]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=color + (alpha,))

    def dot(self, x, y, r, color, alpha=255, glow=True):
        if glow:
            g = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
            gd = ImageDraw.Draw(g)
            gr = r * 2.6 * SS
            gd.ellipse((x * SS - gr, y * SS - gr, x * SS + gr, y * SS + gr), fill=color + (70,))
            g = g.filter(ImageFilter.GaussianBlur(r * 1.2 * SS))
            self.img = Image.alpha_composite(self.img, g)
            self.d = ImageDraw.Draw(self.img)
        rr = r * SS
        self.d.ellipse((x * SS - rr, y * SS - rr, x * SS + rr, y * SS + rr), fill=color + (alpha,))

    def poly_fill(self, pts, color, alpha):
        p = [(x * SS, y * SS) for x, y in pts]
        self.d.polygon(p, fill=color + (alpha,))

    def done(self):
        return self.img.resize((self.w, self.h), Image.BOX)


def smooth(t):
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- TONE
def tone_frame(v):
    """Low-pass response. Corner goes from x=28 to x=124 along a log-ish axis."""
    c = Canvas(CELL_W, CELL_H)
    x0, x1, ytop, ybot = 14, 146, 30, 88
    c.line([(x0, ybot + 8), (x1, ybot + 8)], TRACK, 1.5)
    for tick in range(5):
        tx = x0 + (x1 - x0) * tick / 4
        c.line([(tx, ybot + 8), (tx, ybot + 12)], TRACK, 1.5)
    corner = x0 + 20 + (x1 - x0 - 36) * (v ** 0.8)
    pts = []
    n = 120
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        u = (x - corner) / 22.0  # octave-ish distance past the corner
        drop = 0.0 if u < 0 else min(1.0, u * u * 0.55 + u * 0.35)
        bump = 4.0 * math.exp(-((x - corner + 4) / 7.0) ** 2)  # slight resonant lift
        y = ytop - bump + (ybot - ytop) * drop
        pts.append((x, y))
    area = pts + [(x1, ybot + 8), (x0, ybot + 8)]
    c.poly_fill(area, BLUE, 28)
    c.line(pts, BLUE, 2.2)
    cy = ytop - 4.0 * math.exp(-(-4 / 7.0) ** 2)
    c.dot(corner, cy + 0.0, 4.0, BLUE)
    return c.done()


# ---------------------------------------------------------------- TURNS
def turns_frame(v):
    """A coil seen from the side. 1.5 turns at rest-low up to 8 turns."""
    c = Canvas(CELL_W, CELL_H)
    x0, x1, cy = 16, 144, 58
    turns = 1.5 + 6.5 * v
    amp = 22 - 6 * v
    c.line([(x0 - 2, cy), (x0 + 6, cy)], GREEN, 2.0)
    c.line([(x1 - 6, cy), (x1 + 2, cy)], GREEN, 2.0)
    pts = []
    n = 400
    xa, xb = x0 + 6, x1 - 6
    for i in range(n + 1):
        t = i / n
        ph = 2 * math.pi * turns * t
        # prolate cycloid: the loop leans forward like a real coil seen at an angle
        x = xa + (xb - xa) * t + 5.5 * math.sin(ph)
        y = cy + amp * -math.cos(ph) * 0.0 + amp * math.cos(ph - math.pi / 2) * 1.0
        pts.append((x, y))
    c.line(pts, GREEN, 2.0)
    c.dot(xb + 8, cy, 3.0, GREEN, glow=False)
    return c.done()


# ---------------------------------------------------------------- DAMP
def damp_frame(v):
    """Echo comb with an exponential envelope. Higher DAMP means a quicker fade."""
    c = Canvas(CELL_W, CELL_H)
    x0, x1, ybase, hmax = 14, 146, 88, 56
    c.line([(x0, ybase + 6), (x1, ybase + 6)], TRACK, 1.5)
    tau = 1.0 - 0.82 * v  # fraction of the width over which the level falls by 1/e (scaled below)
    n = 14
    env_pts = []
    for i in range(n):
        t = i / (n - 1)
        x = x0 + 4 + (x1 - x0 - 8) * t
        h = hmax * math.exp(-t / (0.12 + 0.55 * tau))
        c.line([(x, ybase), (x, ybase - max(h, 1.5))], LAV, 2.0, alpha=int(110 + 145 * h / hmax))
        env_pts.append((x, ybase - h))
    c.line(env_pts, LAV, 1.2, alpha=150)
    c.dot(env_pts[0][0], env_pts[0][1], 3.4, LAV)
    return c.done()


# ---------------------------------------------------------------- LEVEL
def level_frame(v):
    """The OP-1 dashed column. Dots start close together and pull apart with LEVEL."""
    c = Canvas(CELL_W, CELL_H)
    cx, mid = CELL_W / 2, 55
    half = 7 + 35 * v
    top, bot = mid - half, mid + half
    y = bot - 4
    while y > top + 4:
        c.line([(cx, y), (cx, max(y - 3, top + 4))], ORANGE, 1.6, alpha=190)
        y -= 5
    r = 2.6 + 1.4 * v
    c.dot(cx, top, r, ORANGE)
    c.dot(cx, bot, r, ORANGE)
    # faint outer rails show the full travel
    c.line([(cx - 14, mid - 44), (cx + 14, mid - 44)], TRACK, 1.5)
    c.line([(cx - 14, mid + 44), (cx + 14, mid + 44)], TRACK, 1.5)
    return c.done()


# ---------------------------------------------------------------- ACT (signal)
ACT_W, ACT_H = 560, 190
# coil centre lines in frame coordinates, measured from the faceplate (frame origin = faceplate 380,222)
COILS = [((10, 100), (515, 8)), ((35, 163), (550, 63))]


def act_frame(ks, ls):
    img = Image.new("RGBA", (ACT_W, ACT_H), (0, 0, 0, 0))
    if ls == 0:
        return img
    lv = ls / 7.0
    tint = 0.45 * ks / 7.0
    col = tuple(int(GREEN[i] * (1 - tint) + BLUE[i] * tint) for i in range(3))
    rng = random.Random(ks * 8 + ls)
    for ci, ((ax, ay), (bx, by)) in enumerate(COILS):
        glow = Image.new("L", (ACT_W, ACT_H), 0)
        gd = ImageDraw.Draw(glow)
        length = 0.16 + 0.55 * lv  # share of the coil that is lit
        centre = 0.5 + (0.12 if ci else -0.12) * math.sin(ls * 1.3 + ks)  # sloshes slightly per level step
        steps = 60
        for i in range(steps + 1):
            t = i / steps
            d = abs(t - centre) / (length / 2)
            w = max(0.0, 1.0 - d * d)
            x, y = ax + (bx - ax) * t, ay + (by - ay) * t
            r = 14 + 14 * lv
            gd.ellipse((x - r, y - r * 0.8, x + r, y + r * 0.8), fill=int(255 * w * w))
        # a fainter wave reflected from the far end
        rc = 1.0 - centre
        for i in range(steps + 1):
            t = i / steps
            d = abs(t - rc) / (length * 0.3)
            w = max(0.0, 1.0 - d * d)
            x, y = ax + (bx - ax) * t, ay + (by - ay) * t
            gd.ellipse((x - 9, y - 7, x + 9, y + 7), fill=int(110 * w))
        glow = glow.filter(ImageFilter.GaussianBlur(9))
        alpha = glow.point(lambda p: int(p * (0.30 + 0.30 * lv)))
        layer = Image.new("RGBA", (ACT_W, ACT_H), col + (0,))
        layer.putalpha(alpha)
        img = Image.alpha_composite(img, layer)
    # sparkle beads, a few more as the level rises, in the lit area only
    spark = Image.new("RGBA", (ACT_W, ACT_H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(spark)
    for _ in range(int(1 + 5 * lv)):
        (ax, ay), (bx, by) = COILS[rng.randrange(2)]
        t = 0.5 + (rng.random() - 0.5) * (0.2 + 0.6 * lv)
        x, y = ax + (bx - ax) * t, ay + (by - ay) * t + rng.uniform(-6, 6)
        sd.ellipse((x - 1.6, y - 1.6, x + 1.6, y + 1.6), fill=(220, 255, 240, int(120 + 100 * lv)))
    spark = spark.filter(ImageFilter.GaussianBlur(0.8))
    return Image.alpha_composite(img, spark)


def main():
    for name, fn in (("tone.png", tone_frame), ("turns.png", turns_frame), ("damp.png", damp_frame),
                     ("level.png", level_frame)):
        save_strip([fn(i / (KNOB_FRAMES - 1)) for i in range(KNOB_FRAMES)], name)
    save_strip([act_frame(k, l) for k in range(8) for l in range(8)], "act.png")


if __name__ == "__main__":
    main()
