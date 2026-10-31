#!/usr/bin/env python3
"""Regenerates every animation strip for the Punch MPC skin. Run: python3 make_art.py
Strips are vertical stacks of RGBA frames on a transparent background (frame 0 on top).

Mapping, after the OP-1 mllp screen (display/mllp.svg):
  freq.png   FREQ    needle sweeping a tick dial (OP-1: blue needle on green ticks, recovered)
  punch.png  PUNCH   burst of rays growing from a point (OP-1: purple rays by the glove, mapping designed)
  rounds.png ROUNDS  ladder of bars filling up (OP-1: ten green bars, recovered; here 24 cells for 24 rounds)
  power.png  POWER   shock arcs travelling out from the glove (designed, the OP-1 shows red glove art)
  act.png    signal  forward glove extends on transients and returns to guard (designed)
  bg.png     MPC faceplate with the forward glove removed, so it can move cleanly
"""
import math
import os

import cv2
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
SS = 3  # supersampling factor

VIOLET = (167, 123, 255)
RED = (255, 77, 94)
RED_HI = (255, 154, 164)
MINT = (46, 240, 160)
TRACK = (58, 63, 74)


def new(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def col(c, a):
    return (c[0], c[1], c[2], int(max(0, min(1, a)) * 255))


def finish(layers, w, h):
    """layers: list of (image, glow_radius_px or 0). Composite with glow, downsample."""
    base = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    for im, glow in layers:
        if glow:
            g = im.filter(ImageFilter.GaussianBlur(glow * SS))
            base = Image.alpha_composite(base, g)
        base = Image.alpha_composite(base, im)
    return base.resize((w, h), Image.LANCZOS)


def stack(frames, name):
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    assert strip.height <= 12288, (name, strip.height)
    strip.save(os.path.join(OUT, name), optimize=True)
    print(name, strip.size, len(frames), "frames")


def line(d, p0, p1, width, color):
    d.line([(p0[0] * SS, p0[1] * SS), (p1[0] * SS, p1[1] * SS)], fill=color, width=int(width * SS))
    r = width * SS / 2
    for p in (p0, p1):
        d.ellipse([p[0] * SS - r, p[1] * SS - r, p[0] * SS + r, p[1] * SS + r], fill=color)


def arc(d, c, r, a0, a1, width, color):
    d.arc([(c[0] - r) * SS, (c[1] - r) * SS, (c[0] + r) * SS, (c[1] + r) * SS], a0, a1, fill=color,
          width=int(width * SS))


def dot(d, c, r, color):
    d.ellipse([(c[0] - r) * SS, (c[1] - r) * SS, (c[0] + r) * SS, (c[1] + r) * SS], fill=color)


# FREQ: needle on a 270 degree tick dial ---------------------------------------------------------
def freq_frames(n=33):
    w = h = 130
    c = (65, 68)
    out = []
    for f in range(n):
        v = f / (n - 1)
        ang = 135 + 270 * v
        ticks = new(w, h)
        td = ImageDraw.Draw(ticks)
        nt = 13
        for i in range(nt):
            a = math.radians(135 + 270 * i / (nt - 1))
            near = max(0.0, 1 - abs(i / (nt - 1) - v) * (nt - 1) / 1.6)
            r0, r1 = 47, 54 if i % 3 == 0 else 52
            p0 = (c[0] + r0 * math.cos(a), c[1] + r0 * math.sin(a))
            p1 = (c[0] + r1 * math.cos(a), c[1] + r1 * math.sin(a))
            colr = tuple(int(TRACK[k] + (VIOLET[k] - TRACK[k]) * near) for k in range(3))
            line(td, p0, p1, 2.4, col(colr, 0.9))
        needle = new(w, h)
        nd = ImageDraw.Draw(needle)
        a = math.radians(ang)
        tip = (c[0] + 40 * math.cos(a), c[1] + 40 * math.sin(a))
        tail = (c[0] - 8 * math.cos(a), c[1] - 8 * math.sin(a))
        line(nd, tail, tip, 3.2, col(VIOLET, 1))
        dot(nd, c, 6, col(VIOLET, 1))
        dot(nd, c, 2.4, col((13, 14, 16), 1))
        out.append(finish([(ticks, 0), (needle, 3)], w, h))
    return out


# PUNCH: burst of rays ----------------------------------------------------------------------------
def punch_frames(n=33):
    w = h = 130
    c = (65, 65)
    out = []
    for f in range(n):
        v = f / (n - 1)
        im = new(w, h)
        d = ImageDraw.Draw(im)
        for i in range(8):
            a = math.radians(i * 45 + 22.5)
            long_ = 1.0 if i % 2 == 0 else 0.62
            r0 = 15
            r1 = r0 + 3 + 42 * v * long_
            p0 = (c[0] + r0 * math.cos(a), c[1] + r0 * math.sin(a))
            p1 = (c[0] + r1 * math.cos(a), c[1] + r1 * math.sin(a))
            line(d, p0, p1, 3.0, col(RED, 0.45 + 0.55 * v))
        arc(d, c, 9, 0, 360, 2.4, col(RED_HI, 0.5 + 0.5 * v))
        dot(d, c, 3 + 2 * v, col(RED_HI, 0.9))
        out.append(finish([(im, 2.5 * v + 0.01)], w, h))
    return out


# ROUNDS: ladder, 24 cells in two columns -----------------------------------------------------------
def rounds_frames(n=33):
    w, h = 120, 150
    out = []
    for f in range(n):
        v = f / (n - 1)
        rounds = 1 + int(round(v * 23))
        im = new(w, h)
        d = ImageDraw.Draw(im)
        lit = new(w, h)
        ld = ImageDraw.Draw(lit)
        for i in range(24):
            colm, row = divmod(i, 12)
            x0 = 8 + colm * 55
            y0 = h - 9 - (row + 1) * 12 + 2
            box = [x0 * SS, y0 * SS, (x0 + 46) * SS, (y0 + 8) * SS]
            if i < rounds:
                top = i == rounds - 1
                ld.rounded_rectangle(box, radius=2 * SS, fill=col(MINT, 1.0 if top else 0.78))
            else:
                d.rounded_rectangle(box, radius=2 * SS, fill=col(TRACK, 0.55))
        out.append(finish([(im, 0), (lit, 3)], w, h))
    return out


# POWER: shock arcs travelling out ---------------------------------------------------------------------
def power_frames(n=33):
    w = h = 130
    c = (14, 65)
    out = []
    for f in range(n):
        v = f / (n - 1)
        guide = new(w, h)
        gd = ImageDraw.Draw(guide)
        live = new(w, h)
        ld = ImageDraw.Draw(live)
        lit_count = 0.6 + 4.4 * v
        for i in range(5):
            r = 20 + 22 * i
            half = 38 - 3 * i
            arc(gd, c, r, -half, half, 2.4, col(TRACK, 0.55))
            k = max(0.0, min(1.0, lit_count - i))
            if k > 0:
                colr = tuple(int(RED[j] + (RED_HI[j] - RED[j]) * i / 4) for j in range(3))
                arc(ld, c, r, -half, half, 3.0 + 1.5 * v, col(colr, 0.3 + 0.7 * k))
        dot(ld, c, 5 + 2 * v, col(RED, 1))
        out.append(finish([(guide, 0), (live, 2.5 * v + 0.01)], w, h))
    return out


# ACT: front glove translation. frame = knob step * 8 + transient step.
def glove_layers():
    source = Image.open(os.path.join(HERE, "../design/art/faceplate-bg-1280x630.png")).convert("RGB")
    # Trace the forward glove and cuff. The shoulder stays on the faceplate.
    outline = [(676, 185), (704, 169), (744, 171), (782, 178), (827, 192),
               (863, 216), (876, 253), (867, 298), (841, 329), (794, 340),
               (748, 333), (704, 309), (675, 280), (661, 242), (663, 210)]
    mask = Image.new("L", source.size, 0)
    ImageDraw.Draw(mask).polygon(outline, fill=255)
    glove = source.convert("RGBA")
    glove.putalpha(mask)
    clean = cv2.inpaint(np.asarray(source), np.asarray(mask), 11, cv2.INPAINT_TELEA)
    bg = Image.fromarray(clean)
    bg.save(os.path.join(OUT, "bg.png"), optimize=True)
    return glove.crop((652, 165, 892, 355))


def act_frames():
    w, h = 240, 190
    glove = glove_layers()
    out = []
    for k in range(8):
        for l in range(8):
            # The lead knob occupies the high three frame bits; only the onset level moves the arm.
            shift = round(12 * (l / 7.0) ** 0.75)
            frame = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            frame.alpha_composite(glove, (shift, -shift // 5))
            out.append(frame)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    stack(freq_frames(), "freq.png")
    stack(punch_frames(), "punch.png")
    stack(rounds_frames(), "rounds.png")
    stack(power_frames(), "power.png")
    stack(act_frames(), "act.png")


if __name__ == "__main__":
    main()
