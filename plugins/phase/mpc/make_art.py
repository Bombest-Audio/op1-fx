#!/usr/bin/env python3
"""Regenerate every MPC animation strip for Phase into images/. Run from anywhere: python3 mpc/make_art.py

Stage is 800x190 (page x 240..1040, y 235..425). Layers, bottom to top:
  act.png  64 frames (knob step * 8 + level step): panel, axes, ghosts, level glow. Only the level axis changes.
  a.png    blue positive half cycle, peak skews and the dot rides the peak with knob A (33 frames)
  b.png    green negative half cycle, same with knob B
  w.png    red four-cycle wave, each cycle fades towards its end by knob W
  r.png    white dotted track, filled up to the head dot by knob R
The shapes follow the OP-1 phase screen (display/pd.svg): blue and green half cycles with end dots, a red burst wave,
a white dotted line with a head dot. The knob to shape mapping is my own, drawn in my own style.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent / "images"
SS = 2  # supersampling
N = 33
BLUE, GREEN, WHITE, RED = (105, 142, 255), (0, 237, 149), (223, 217, 255), (255, 58, 93)
GREY = (101, 101, 121)
SW, SH = 800, 190
BASE = 95  # stage y of the baseline
LX0, LX1 = 40, 340  # left panel cycle, split at 190
RX0, RX1 = 430, 770  # right panel wave span


def new(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def done(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def poly(d, pts, col, width, ox=0, oy=0, alpha=255):
    p = [((x - ox) * SS, (y - oy) * SS) for x, y in pts]
    d.line(p, fill=col + (alpha,), width=int(width * SS), joint="curve")
    r = width * SS / 2
    for x, y in (p[0], p[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=col + (alpha,))


def dot(d, x, y, r, col, ox=0, oy=0, alpha=255):
    x, y, r = (x - ox) * SS, (y - oy) * SS, r * SS
    d.ellipse((x - r, y - r, x + r, y + r), fill=col + (alpha,))


def half(a, x0, x1, amp, sign, steps=90):
    """Half cycle of a bent sine. g in 0.5..2 moves the peak: a=0.5 centre, small a peaks late, large a peaks early."""
    g = 2.0 ** ((0.5 - a) * 2.0)
    pts = []
    for i in range(steps + 1):
        t = i / steps
        pts.append((x0 + (x1 - x0) * t, BASE - sign * amp * math.sin(math.pi * t ** g)))
    peak_t = 0.5 ** (1.0 / g)
    return pts, (x0 + (x1 - x0) * peak_t, BASE - sign * amp)


def half_strip(name, sign, colour):
    fw, fh = 320, 94
    oy = BASE - 89 if sign > 0 else BASE - 5  # frame top in stage y
    ox = 30
    sheet = Image.new("RGBA", (fw, fh * N), (0, 0, 0, 0))
    for i in range(N):
        a = i / (N - 1)
        im = new(fw, fh)
        d = ImageDraw.Draw(im)
        x0, x1 = (LX0, 190) if sign > 0 else (190, LX1)
        pts, peak = half(a, x0, x1, 80, sign)
        poly(d, pts, colour, 3, ox, oy)
        dot(d, x0, BASE, 4, colour, ox, oy)
        dot(d, x1, BASE, 4, colour, ox, oy)
        dot(d, peak[0], peak[1], 5, colour, ox, oy)
        sheet.paste(done(im, fw, fh), (0, i * fh))
    sheet.save(OUT / name)
    return (ox, oy, fw, fh)


def red_wave(w_amt, ghost=False):
    pts = []
    cycles = 4
    n = 240
    for i in range(n + 1):
        t = i / n
        c = t * cycles
        frac = c - math.floor(c) if i < n else 1.0
        win = 1.0 if ghost else 1.0 - w_amt * 0.92 * frac
        pts.append((RX0 + (RX1 - RX0) * t, BASE + 8 - 52 * win * math.sin(2 * math.pi * c)))
    return pts


def w_strip():
    fw, fh = 360, 130
    ox, oy = 420, 30
    sheet = Image.new("RGBA", (fw, fh * N), (0, 0, 0, 0))
    for i in range(N):
        im = new(fw, fh)
        d = ImageDraw.Draw(im)
        pts = red_wave(i / (N - 1))
        poly(d, pts, RED, 3, ox, oy)
        dot(d, pts[0][0], pts[0][1], 5, RED, ox, oy)
        sheet.paste(done(im, fw, fh), (0, i * fh))
    sheet.save(OUT / "w.png")
    return (ox, oy, fw, fh)


def r_strip():
    fw, fh = 380, 24
    ox, oy = 410, 2
    y = 14
    sheet = Image.new("RGBA", (fw, fh * N), (0, 0, 0, 0))
    track = list(range(RX0, RX1 - 8, 17))
    for i in range(N):
        r = i / (N - 1)
        im = new(fw, fh)
        d = ImageDraw.Draw(im)
        head = RX0 + (RX1 - RX0) * r
        for x in track:
            if x + 10 <= head:
                poly(d, [(x, y), (x + 10, y)], WHITE, 3, ox, oy)
        dot(d, head, y, 5, WHITE, ox, oy)
        sheet.paste(done(im, fw, fh), (0, i * fh))
    sheet.save(OUT / "r.png")
    return (ox, oy, fw, fh)


def act_frame(level):
    """Static plate plus the level response: border, axes and glow come up with the signal."""
    L = level ** 0.8
    im = new(SW, SH)
    d = ImageDraw.Draw(im)
    for px0, px1 in ((8, 392), (408, 792)):
        d.rounded_rectangle((px0 * SS, 4 * SS, px1 * SS, (SH - 4) * SS), radius=10 * SS, fill=(7, 8, 12, 150))
    glow = Image.new("RGBA", im.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    ga = int(110 * L)
    if ga:
        gd.ellipse((40 * SS, 12 * SS, 190 * SS, 100 * SS), fill=BLUE + (ga,))
        gd.ellipse((190 * SS, 90 * SS, 340 * SS, 178 * SS), fill=GREEN + (ga,))
        gd.ellipse((440 * SS, 40 * SS, 760 * SS, 150 * SS), fill=RED + (int(ga * 0.8),))
        glow = glow.filter(ImageFilter.GaussianBlur((10 + 14 * L) * SS))
        im.alpha_composite(glow)
        d = ImageDraw.Draw(im)
    edge = int(40 + 150 * L)
    for px0, px1, col in ((8, 392, BLUE), (408, 792, RED)):
        d.rounded_rectangle((px0 * SS, 4 * SS, px1 * SS, (SH - 4) * SS), radius=10 * SS,
                            outline=col + (edge,), width=SS * (1 if L < 0.5 else 2))
    ax = int(110 + 100 * L)
    for x in (28, 416):
        poly(d, [(x, 168), (x, 24)], GREY, 1.5, alpha=ax)
        d.polygon([((x - 3) * SS, 28 * SS), (x * SS, 22 * SS), ((x + 3) * SS, 28 * SS)], fill=GREY + (ax,))
        dot(d, x, BASE, 3, GREY, alpha=ax)
        dot(d, x, 168, 3, GREY, alpha=ax)
    poly(d, [(28, BASE), (352, BASE)], GREY, 1.5, alpha=ax)
    poly(d, [(416, BASE + 8), (780, BASE + 8)], GREY, 1.5, alpha=ax)
    # ghosts: dim teal tracks under the green half cycle, dim red full amplitude wave, dim white track
    for x in range(52, 180, 12):
        poly(d, [(x, 138), (x + 7, 138)], (0, 71, 91), 1.5, alpha=int(150 + 100 * L))
    poly(d, red_wave(0, ghost=True), (81, 33, 50), 1.5, alpha=int(160 + 95 * L))
    for x in range(RX0, RX1 - 8, 17):
        poly(d, [(x, 14), (x + 10, 14)], WHITE, 3, alpha=int(35 + 55 * L))
    return done(im, SW, SH)


def act_strip():
    sheet = Image.new("RGBA", (SW, SH * 64), (0, 0, 0, 0))
    for l in range(8):
        fr = act_frame(l / 7)
        for k in range(8):
            sheet.paste(fr, (0, (k * 8 + l) * SH))
    sheet.save(OUT / "act.png")


def main():
    OUT.mkdir(exist_ok=True)
    act_strip()
    for name, info in (("a.png", half_strip("a.png", 1, BLUE)), ("b.png", half_strip("b.png", -1, GREEN)),
                       ("w.png", w_strip()), ("r.png", r_strip())):
        ox, oy, fw, fh = info
        print(name, "stage rect", ox, oy, fw, fh, "-> cx", 240 + ox + fw / 2, "cy", 235 + oy + fh / 2)


if __name__ == "__main__":
    main()
