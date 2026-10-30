#!/usr/bin/env python3
"""Regenerates every MPC image for DSynth: images/act.png and the four knob filmstrips.

Run from anywhere: python3 make_art.py
Layout: a row of four small graphics above the pad (MOD, WAVE, DECAY, FILTER, in that order)
plus a ring pulse over the pad driven by the engine's `act` parameter.
Redrawn in my own style, loosely after the OP-1 dsynth screen: the MOD ring pair, the
oscillator wave selector column and the stepped filter.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
SS = 4  # supersample

BLUE = (0x69, 0x8E, 0xFF)
GREEN = (0x2E, 0xF0, 0xA0)
WHITE = (0xE9, 0xEC, 0xE8)
ORANGE = (0xFF, 0x9A, 0x3D)
TRACK = (0x3A, 0x3F, 0x44)
CYAN = (0x2F, 0xB4, 0xF0)  # the pad art's ring colour

KW, KH, KFRAMES = 150, 90, 33


def lerp(a, b, t):
    return a + (b - a) * t


def col(c, a):
    return (c[0], c[1], c[2], int(max(0, min(255, a))))


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def line(self, pts, c, width=1.5):
        p = [(x * SS, y * SS) for x, y in pts]
        self.d.line(p, fill=c, width=max(1, int(width * SS)), joint="curve")

    def circle(self, cx, cy, r, outline=None, fill=None, width=1.5):
        box = [(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS]
        self.d.ellipse(box, outline=outline, fill=fill, width=max(1, int(width * SS)))

    def poly(self, pts, fill):
        self.d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)

    def finish(self, glow=None):
        im = self.im.resize((self.w, self.h), Image.LANCZOS)
        if glow:
            g = im.filter(ImageFilter.GaussianBlur(glow))
            out = Image.new("RGBA", im.size, (0, 0, 0, 0))
            out = Image.alpha_composite(out, g)
            im = Image.alpha_composite(out, im)
        return im


def strip(frames):
    w, h = frames[0].size
    s = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        s.paste(f, (0, i * h))
    assert s.size[1] <= 12288, s.size
    return s


# ---- MOD: ring pair. The inner ring (the OP-1 "redring") grows toward the outer ring with the amount.
def mod_frame(t):
    c = Canvas(KW, KH)
    cx, cy = KW / 2, KH / 2
    R = 34
    c.circle(cx, cy, R, outline=col(WHITE, 90), width=1.5)
    r_in = lerp(3, R - 3, t)
    c.circle(cx, cy, r_in, fill=col(BLUE, 40 + 60 * t), outline=col(BLUE, 255), width=1.8)
    c.circle(cx, cy, 2, fill=col(BLUE, 255))
    # small ticks at the outer ring show the range
    for k in range(8):
        a = k * math.pi / 4
        c.line([(cx + (R + 3) * math.cos(a), cy + (R + 3) * math.sin(a)),
                (cx + (R + 6) * math.cos(a), cy + (R + 6) * math.sin(a))], col(WHITE, 60), 1.2)
    return c.finish(glow=3)


# ---- WAVE: waveform glyph morphing sine > triangle > saw > square, plus the 4-dot selector.
def wave_sample(x, t):
    # x in 0..1 across one cycle, t in 0..1 over four shapes
    sine = math.sin(2 * math.pi * x)
    tri = 4 * abs(x - 0.5) - 1
    tri = -tri if False else (1 - 4 * abs(((x + 0.25) % 1) - 0.5))
    saw = 2 * x - 1 if x < 1 else 1
    sq = 1.0 if x < 0.5 else -1.0
    shapes = [sine, tri, saw, sq]
    pos = t * 3
    i = min(2, int(pos))
    f = pos - i
    return lerp(shapes[i], shapes[i + 1], f)


def wave_frame(t):
    c = Canvas(KW, KH)
    x0, x1, cy, amp = 20, 130, 36, 20
    pts = []
    n = 220
    for k in range(n + 1):
        x = k / n
        pts.append((lerp(x0, x1, x), cy - amp * wave_sample((x * 2) % 1.0 if k < n else 0.999, t)))
    c.line([(x0, cy), (x1, cy)], col(TRACK, 140), 1.0)
    c.line(pts, col(GREEN, 255), 1.8)
    # selector column turned sideways: four dots, the lit one glides between them
    dx = [38 + i * 25 for i in range(4)]
    for i, x in enumerate(dx):
        c.circle(x, 74, 3, fill=col(TRACK, 255))
    px = lerp(dx[0], dx[3], t)
    c.circle(px, 74, 4.5, fill=col(GREEN, 255))
    return c.finish(glow=3)


# ---- DECAY: exponential tail. Longer decay = slower fall, brighter fill.
def decay_frame(t):
    c = Canvas(KW, KH)
    x0, x1, yb, yt = 15, 135, 74, 14
    tau = lerp(0.07, 0.9, t)
    pts = []
    n = 120
    for k in range(n + 1):
        x = k / n
        pts.append((lerp(x0, x1, x), lerp(yb, yt, math.exp(-x / tau))))
    c.poly(pts + [(x1, yb), (x0, yb)], col(WHITE, 22 + 40 * t))
    c.line([(x0, yb), (x1, yb)], col(TRACK, 200), 1.0)
    c.line(pts, col(WHITE, 255), 1.8)
    c.circle(x0, yt, 3, fill=col(WHITE, 255))
    return c.finish(glow=2.5)


# ---- FILTER: 8 steps (0..7). Response curve with a cutoff that steps along the track; the dot rides the curve.
def filter_frame(t):
    c = Canvas(KW, KH)
    x0, x1, yb = 15, 135, 70
    cut = lerp(0.12, 0.88, t)
    def resp(x):
        # 2-pole lowpass shape with a resonant bump just before the cutoff
        d = x - cut
        bump = 10 * math.exp(-((d + 0.03) ** 2) / 0.004)
        fall = 0 if d < 0 else 70 * d * 1.6
        return 34 - bump + min(fall, 40)
    pts = [(lerp(x0, x1, k / 120), resp(k / 120)) for k in range(121)]
    c.line(pts, col(ORANGE, 255), 1.8)
    # eight ticks, the active one tall
    step = round(t * 7)
    for i in range(8):
        x = lerp(x0 + 6, x1 - 6, i / 7)
        on = i == step
        c.line([(x, yb + 4), (x, yb + (12 if on else 8))], col(ORANGE if on else TRACK, 255 if on else 220), 1.5)
    cx = lerp(x0, x1, cut)
    c.circle(cx, resp(cut), 4, fill=col(ORANGE, 255))
    return c.finish(glow=3)


# ---- ACT: rings over the pad. Level axis = pulse; knob axis (mod) = ring spacing and thickness.
AW = AH = 180


def act_frame(frame):
    kn, lv = frame // 8, frame % 8
    k, l = kn / 7.0, lv / 7.0
    c = Canvas(AW, AH)
    cx = cy = AW / 2
    n = 4
    for i in range(n):
        spacing = lerp(15, 21, k)
        r = 14 + i * spacing + l * (10 + 4 * i)  # rings push outward with level
        if r > 86:
            continue
        a = (0.18 + 0.82 * l) * (1.0 - i / (n + 0.6)) * 255
        c.circle(cx, cy, r, outline=col(CYAN, a), width=lerp(1.2, 2.0, l))
    c.circle(cx, cy, 6 + 5 * l, fill=col(CYAN, 30 + 170 * l))
    return c.finish(glow=lerp(1.5, 5, l))


def main():
    os.makedirs(OUT, exist_ok=True)
    jobs = {
        "mod.png": mod_frame,
        "wave.png": wave_frame,
        "decay.png": decay_frame,
        "filter.png": filter_frame,
    }
    for name, fn in jobs.items():
        strip([fn(i / (KFRAMES - 1)) for i in range(KFRAMES)]).save(os.path.join(OUT, name))
    strip([act_frame(i) for i in range(64)]).save(os.path.join(OUT, "act.png"))
    print("ok")


if __name__ == "__main__":
    main()
