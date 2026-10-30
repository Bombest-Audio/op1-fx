#!/usr/bin/env python3
"""Regenerates every MPC animation strip for Delay. Run from anywhere: python3 make_art.py

Layers (all transparent RGBA filmstrips, drawn over the faceplate):
  images/act.png    64 frames 500x180, engine driven: frame = TIME step * 8 + level step. Soft blue glow at the ripple
                    centre that rises with the echo level and swells slightly with TIME.
  images/rings.png  33 frames 360x360, key=time: five nested blue rings (the OP-1 Delay screen ring field). TIME opens
                    the field from tight to wide.
  images/fdb.png    33 frames 360x360, key=fdb: lavender arcs trailing around each ring, longer with more feedback.
  images/speed.png  33 frames 360x360, key=speed: green dot orbiting the middle ring (tape speed).
  images/input.png  33 frames 360x24, key=input: red dot travelling along a short track into the centre.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent / "images"
SS = 3  # supersampling
BLUE, GREEN, WHITE, RED = (105, 142, 255), (0, 237, 149), (223, 217, 255), (255, 58, 93)
# ring radii and centre offsets measured from the OP-1 Delay screen, relative to the largest ring (radius 69.4)
RADII = [9.3, 27.3, 44.3, 56.8, 69.4]
CENTRES = [(22.0, 17.4), (22.0, 17.4), (20.1, 15.6), (10.0, 7.5), (0.0, 0.0)]
N = 33
S = 360


def ring_geometry(t):
    """(cx, cy, r) per ring at TIME t in 0..1, in 360px frame coordinates, centred as a group."""
    k = 0.30 + 0.50 * t
    scale = 168.0 / 69.4 * k
    out = []
    for r, (dx, dy) in zip(RADII, CENTRES):
        out.append((-dx * scale + 0, -dy * scale + 0, r * scale))
    # centre the group on the middle of the frame
    xs = [c[0] - c[2] for c in out] + [c[0] + c[2] for c in out]
    ys = [c[1] - c[2] for c in out] + [c[1] + c[2] for c in out]
    ox = S / 2 - (min(xs) + max(xs)) / 2
    oy = S / 2 - (min(ys) + max(ys)) / 2
    return [(cx + ox, cy + oy, r) for cx, cy, r in out]


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def down(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def circle_box(cx, cy, r):
    return ((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS)


def sheet(frames, w, h, path):
    sh = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sh.paste(f, (0, i * h))
    assert sh.size[1] <= 12288, (path, sh.size)
    sh.save(OUT / path)


def make_rings():
    fr = []
    for i in range(N):
        t = i / (N - 1)
        im = canvas(S, S)
        d = ImageDraw.Draw(im)
        for n, (cx, cy, r) in enumerate(ring_geometry(t)):
            a = 70 + 22 * n
            d.ellipse(circle_box(cx, cy, r), outline=BLUE + (a,), width=2 * SS)
        fr.append(down(im, S, S))
    sheet(fr, S, S, "rings.png")


def make_fdb():
    fr = []
    for i in range(N):
        f = i / (N - 1)
        im = canvas(S, S)
        d = ImageDraw.Draw(im)
        for n, (cx, cy, r) in enumerate(ring_geometry(0.244)):
            # each outer ring trails less, like later echoes dying away
            sweep = 330.0 * f * (1.0 - 0.13 * n)
            if sweep < 3:
                continue
            a = int(200 * (0.55 + 0.45 * f) * (1.0 - 0.1 * n))
            d.arc(circle_box(cx, cy, r), -90, -90 + sweep, fill=WHITE + (a,), width=2 * SS)
        fr.append(down(im, S, S))
    sheet(fr, S, S, "fdb.png")


def make_speed():
    fr = []
    cx, cy, r = ring_geometry(0.244)[2]
    for i in range(N):
        s = i / (N - 1)
        ang = math.radians(-90 + 360.0 * s)
        x, y = cx + r * math.cos(ang), cy + r * math.sin(ang)
        im = canvas(S, S)
        glow = canvas(S, S)
        ImageDraw.Draw(glow).ellipse(circle_box(x, y, 9), fill=GREEN + (90,))
        glow = glow.filter(ImageFilter.GaussianBlur(6 * SS))
        im.alpha_composite(glow)
        ImageDraw.Draw(im).ellipse(circle_box(x, y, 4.2), fill=GREEN + (235,))
        fr.append(down(im, S, S))
    sheet(fr, S, S, "speed.png")


def make_input():
    w, h = 360, 24
    fr = []
    for i in range(N):
        v = i / (N - 1)
        x = 8 + (w - 16) * v
        im = canvas(w, h)
        d = ImageDraw.Draw(im)
        d.line((8 * SS, h / 2 * SS, x * SS, h / 2 * SS), fill=RED + (110,), width=2 * SS)
        d.line((8 * SS, h / 2 * SS, (w - 8) * SS, h / 2 * SS), fill=RED + (30,), width=1 * SS)
        d.ellipse(circle_box(x, h / 2, 4.2), fill=RED + (240,))
        fr.append(down(im, w, h))
    sheet(fr, w, h, "input.png")


def make_act():
    w, h = 500, 180
    fr = []
    for k in range(8):
        for l in range(8):
            lv = l / 7.0
            im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            if l:
                sx = 70 + 18 * k / 7
                sy = 40 + 8 * k / 7
                g = Image.new("RGBA", (w, h), (0, 0, 0, 0))
                a = int(150 * lv ** 1.2)
                ImageDraw.Draw(g).ellipse((w / 2 - sx - 40 * lv, h / 2 - sy - 14 * lv,
                                           w / 2 + sx + 40 * lv, h / 2 + sy + 14 * lv), fill=BLUE + (a,))
                im = g.filter(ImageFilter.GaussianBlur(26))
            fr.append(im)
    sheet(fr, w, h, "act.png")


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    make_act()
    make_rings()
    make_fdb()
    make_speed()
    make_input()
