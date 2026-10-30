#!/usr/bin/env python3
"""Regenerates every image in mpc/images/ for the String MPC skin.

Elements (all drawn from scratch with Pillow, positions are faceplate pixels, add 86 for page y):
  pick.png   blue pluck marker travelling up the centre strings (33 frames, 100x190)
  damp.png   green comb of loop bars, more bars lit as damp rises (33 frames, 130x200)
  decay.png  orange ring-out trail, longer and flatter as decay rises (33 frames, 200x150)
  stiff.png  white partial ladder that stretches out of harmonic spacing (33 frames, 110x300)
  act.png    amber standing wave ghost on the strings, 64 frames = pluck step * 8 + level step (400x110)
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
SS = 3  # supersample
BLUE, MINT, WHITE, ORANGE, AMBER = (90, 160, 255), (46, 240, 160), (233, 236, 232), (255, 154, 61), (255, 179, 71)
DIM = (35, 38, 42)


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def glow(layer, radius, gain=1.0):
    g = layer.filter(ImageFilter.GaussianBlur(radius * SS))
    if gain != 1.0:
        a = g.getchannel("A").point(lambda v: min(255, int(v * gain)))
        g.putalpha(a)
    return Image.alpha_composite(g, layer)


def fin(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def col(c, a):
    return (c[0], c[1], c[2], int(max(0, min(255, a))))


def strip(frames, name, w, h):
    n = len(frames)
    assert h * n <= 12288, (name, h * n)
    s = Image.new("RGBA", (w, h * n), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        s.paste(f, (0, i * h))
    s.save(os.path.join(OUT, name))
    print(name, s.size, n, "frames")


def pick_frame(t, w=100, h=190):
    im = canvas(w, h)
    d = ImageDraw.Draw(im)
    top, bot = 14, h - 14
    # faint track ticks either side of the strings
    for i in range(7):
        y = bot - i * (bot - top) / 6
        for x0, x1 in ((18, 28), (72, 82)):
            d.line([x0 * SS, y * SS, x1 * SS, y * SS], fill=col(BLUE, 55), width=SS)
    y = bot - t * (bot - top)
    cx = w / 2
    d.ellipse([(cx - 7) * SS, (y - 7) * SS, (cx + 7) * SS, (y + 7) * SS], outline=col(BLUE, 200), width=SS)
    d.ellipse([(cx - 3.5) * SS, (y - 3.5) * SS, (cx + 3.5) * SS, (y + 3.5) * SS], fill=col(BLUE, 255))
    # chevrons pointing at the pick point
    for sx in (-1, 1):
        x = cx + sx * 20
        d.line([(x + sx * 4) * SS, (y - 4) * SS, x * SS, y * SS, (x + sx * 4) * SS, (y + 4) * SS], fill=col(BLUE, 170), width=SS)
    return fin(glow(im, 4, 1.4), w, h)


def damp_frame(t, w=130, h=200):
    im = canvas(w, h)
    d = ImageDraw.Draw(im)
    n = 12
    lit = t * n
    for i in range(n):
        y = h - 10 - i * (h - 20) / (n - 1)
        length = 62 if i % 4 == 0 else 38
        x1 = w - 8
        x0 = x1 - length
        d.line([x0 * SS, y * SS, x1 * SS, y * SS], fill=col(DIM, 255), width=3 * SS)
        k = max(0.0, min(1.0, lit - i))
        if k > 0:
            d.line([x0 * SS, y * SS, x1 * SS, y * SS], fill=col(MINT, 70 + 185 * k), width=3 * SS)
    return fin(glow(im, 3, 1.0), w, h)


def decay_frame(t, w=200, h=150):
    im = canvas(w, h)
    d = ImageDraw.Draw(im)
    n = 20
    tau = 1.6 + 16.0 * t * t
    base = h - 14
    d.line([8 * SS, base * SS, (w - 8) * SS, base * SS], fill=col(DIM, 255), width=2 * SS)
    for i in range(n):
        x = 12 + i * (w - 24) / (n - 1)
        a = math.exp(-i / tau)
        bh = 8 + (h - 36) * a
        d.line([x * SS, base * SS, x * SS, (base - bh) * SS], fill=col(ORANGE, 60 + 195 * a), width=int(3.2 * SS))
    return fin(glow(im, 3, 1.0), w, h)


def stiff_frame(t, w=110, h=300):
    im = canvas(w, h)
    d = ImageDraw.Draw(im)
    spacing, B = 20.0, 0.010 * t
    base = h - 12
    for n in range(1, 11):
        yi = base - spacing * n
        d.line([30 * SS, yi * SS, (w - 14) * SS, yi * SS], fill=col(DIM, 255), width=SS)  # ideal harmonic
        y = base - spacing * n * math.sqrt(1 + B * n * n)
        if y < 6:
            continue
        length = (w - 44) * (1.0 - 0.07 * n)
        a = 255 - n * 12
        d.line([(w - 14 - length) * SS, y * SS, (w - 14) * SS, y * SS], fill=col(WHITE, a), width=int(2.4 * SS))
        d.ellipse([(w - 17) * SS, (y - 3) * SS, (w - 11) * SS, (y + 3) * SS], fill=col(WHITE, a))
    return fin(glow(im, 2, 0.9), w, h)


STRINGS = [(425, 632), (520, 634), (603, 630), (677, 646), (755, 654), (850, 662)]  # faceplate x at y=385 and y=40
Y_BASE, Y_TOP = 385.0, 40.0


def act_frame(knob_step, level_step, w=400, h=110, ox=440, oy=285):
    im = canvas(w, h)
    if level_step == 0:
        return fin(im, w, h)
    lvl = level_step / 7.0
    peak = 0.22 + 0.28 * knob_step / 7.0  # pluck position moves the loudest point of the wave
    g = math.log(0.5) / math.log(peak)
    amp = 1.5 + 9.0 * lvl
    d = ImageDraw.Draw(im)
    for xb, xt in STRINGS:
        pts = []
        for k in range(0, h + 1, 2):
            y = oy + k
            s = (Y_BASE - y) / (Y_BASE - Y_TOP)
            x = xb + (xt - xb) * s
            disp = amp * math.sin(math.pi * max(0.0, min(1.0, s)) ** g)
            pts.append(((x + disp - ox) * SS, k * SS))
        d.line(pts, fill=col(AMBER, 60 + 140 * lvl), width=int(2.0 * SS))
    return fin(glow(im, 3 + 3 * lvl, 1.2 + lvl), w, h)


def main():
    os.makedirs(OUT, exist_ok=True)
    N = 33
    strip([pick_frame(i / (N - 1)) for i in range(N)], "pick.png", 100, 190)
    strip([damp_frame(i / (N - 1)) for i in range(N)], "damp.png", 130, 200)
    strip([decay_frame(i / (N - 1)) for i in range(N)], "decay.png", 200, 150)
    strip([stiff_frame(i / (N - 1)) for i in range(N)], "stiff.png", 110, 300)
    strip([act_frame(k, l) for k in range(8) for l in range(8)], "act.png", 400, 110)


if __name__ == "__main__":
    main()
