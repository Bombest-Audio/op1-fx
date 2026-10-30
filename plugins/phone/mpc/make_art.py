#!/usr/bin/env python3
"""Regenerates every animation strip for the Phone MPC skin into mpc/images/.

Each strip is a vertical stack of RGBA frames on a transparent background, drawn over the faceplate art:
  tone.png   33 frames  blue dial: a dot rides a ticked track with TONE (OP-1: the blue pot / dial)
  phonic.png 15 frames  green ring of N dots around the mic capsule (N = PHONIC, cycles per fragment)
  baud.png   31 frames  white pulse train, more cycles as BAUD rises (OP-1: the white chip)
  noise.png  33 frames  red ticks beside the speaker, more and brighter with TELEMATIC (OP-1: the noise ticks)
  act_spk.png / act_mic.png / act_chip.png  64 frames each, level driven glow and arcs (frame = knob step * 8 +
      level step, only the level step is used here, the knob step repeats the same picture)
Run from anywhere: python3 make_art.py
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
SS = 3  # supersampling

BLUE = (107, 140, 255)
MINT = (46, 240, 160)
LILAC = (207, 201, 255)
RED = (255, 61, 90)
DIM = (35, 38, 42)


def new(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def finish(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def glow(im, radius, gain=1.0):
    """Soft halo of an RGBA layer, composited under the sharp layer."""
    b = im.filter(ImageFilter.GaussianBlur(radius * SS))
    if gain != 1.0:
        r, g, bl, a = b.split()
        a = a.point(lambda v: min(255, int(v * gain)))
        b = Image.merge("RGBA", (r, g, bl, a))
    return Image.alpha_composite(b, im)


def rgba(c, a):
    return (c[0], c[1], c[2], int(max(0, min(255, a * 255))))


def stack(frames, name):
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    assert strip.size[1] <= 12288, (name, strip.size)
    strip.save(os.path.join(OUT, name), optimize=True)
    return strip.size


def line(d, p, q, col, wid):
    d.line([(p[0] * SS, p[1] * SS), (q[0] * SS, q[1] * SS)], fill=col, width=int(wid * SS))
    r = wid * SS / 2
    for x, y in (p, q):
        d.ellipse([x * SS - r, y * SS - r, x * SS + r, y * SS + r], fill=col)


def dot(d, c, r, col):
    d.ellipse([(c[0] - r) * SS, (c[1] - r) * SS, (c[0] + r) * SS, (c[1] + r) * SS], fill=col)


# ---------------------------------------------------------------- TONE: dial track with a riding dot
def tone_frame(t, w=180, h=60):
    im = new(w, h)
    d = ImageDraw.Draw(im)
    x0, x1, y = 14, w - 14, 40
    # ticks above the track, the OP-1 dial has three: low, middle, high
    for k, tx in enumerate((x0 + 6, (x0 + x1) / 2, x1 - 6)):
        near = abs((tx - x0) / (x1 - x0) - t) < 0.08
        line(d, (tx, 10 if k == 1 else 14), (tx, 20), rgba(BLUE, 0.95 if near else 0.38), 2)
    px = x0 + (x1 - x0) * t
    line(d, (x0, y), (x1, y), rgba(DIM, 1.0), 2.4)           # empty track
    if px > x0 + 0.5:
        line(d, (x0, y), (px, y), rgba(BLUE, 0.85), 2.4)       # travelled part
    top = new(w, h)
    td = ImageDraw.Draw(top)
    dot(td, (px, y), 5.2, rgba(BLUE, 1.0))
    dot(td, (px, y), 2.0, (255, 255, 255, 235))
    im = Image.alpha_composite(im, glow(top, 4, 1.2))
    return finish(im, w, h)


# ---------------------------------------------------------------- PHONIC: ring of N dots around the mic
def phonic_frame(n, w=170, h=112):
    im = new(w, h)
    d = ImageDraw.Draw(im)
    cx, cy, rx, ry = w / 2, h / 2, 74, 42
    for k in range(15):                                         # faint track slots
        a = -math.pi / 2 + 2 * math.pi * k / 15
        dot(d, (cx + rx * math.cos(a), cy + ry * math.sin(a)), 1.5, rgba(MINT, 0.20))
    lit = new(w, h)
    ld = ImageDraw.Draw(lit)
    for k in range(n):
        a = -math.pi / 2 + 2 * math.pi * k / n
        dot(ld, (cx + rx * math.cos(a), cy + ry * math.sin(a)), 3.6, rgba(MINT, 1.0))
        dot(ld, (cx + rx * math.cos(a), cy + ry * math.sin(a)), 1.4, (235, 255, 245, 235))
    im = Image.alpha_composite(im, glow(lit, 3, 1.1))
    return finish(im, w, h)


# ---------------------------------------------------------------- BAUD: pulse train, cycles grow with baud
def baud_frame(v, w=180, h=60):
    """v 0..1. 1 cycle in the window at the lowest baud, 8 at the highest."""
    im = new(w, h)
    d = ImageDraw.Draw(im)
    x0, x1, ylo, yhi = 12, w - 12, 40, 18
    line(d, (x0, ylo), (x1, ylo), rgba(DIM, 1.0), 2)
    cycles = 1.0 + 7.0 * v
    period = (x1 - x0) / cycles
    pts = [(x0, ylo)]
    x, up = x0, True
    while x < x1:
        nx = min(x1, x + period / 2)
        yy = yhi if up else ylo
        pts += [(x, yy), (nx, yy)]
        x, up = nx, not up
    pts.append((x1, ylo if not up else yhi))
    top = new(w, h)
    td = ImageDraw.Draw(top)
    td.line([(p[0] * SS, p[1] * SS) for p in pts], fill=rgba(LILAC, 0.9), width=int(2 * SS), joint="curve")
    im = Image.alpha_composite(im, glow(top, 3, 0.9))
    return finish(im, w, h)


# ---------------------------------------------------------------- TELEMATIC: noise ticks beside the speaker
# tick centres follow the OP-1 noise cluster (n0..n9), scaled 2.4x about its own centre
OP1_NOISE = [(242.2, 30.0), (242.4, 21.5), (237.8, 34.0), (237.8, 25.7), (233.5, 29.7),
             (233.5, 21.5), (229.3, 34.0), (229.3, 25.7), (225.2, 29.9), (225.2, 21.5)]
# order they wake up: the first ones near the speaker, the rest spreading out
NOISE_ORDER = [4, 5, 6, 7, 8, 9, 2, 3, 0, 1]


def noise_frame(t, w=130, h=100):
    im = new(w, h)
    d = ImageDraw.Draw(im)
    cx0 = sum(p[0] for p in OP1_NOISE) / 10
    cy0 = sum(p[1] for p in OP1_NOISE) / 10
    top = new(w, h)
    td = ImageDraw.Draw(top)
    for rank, idx in enumerate(NOISE_ORDER):
        ox, oy = OP1_NOISE[idx]
        x = w / 2 + (ox - cx0) * 2.6
        y = h / 2 + (oy - cy0) * 2.6
        on = max(0.0, min(1.0, t * 11.0 - rank))              # each tick fades in as TELEMATIC passes its rank
        half = 3.0 + 6.0 * on
        line(d, (x, y - 3.0), (x, y + 3.0), rgba(RED, 0.16), 2)   # resting stub, so rest is never empty
        if on > 0:
            line(td, (x, y - half), (x, y + half), rgba(RED, 0.35 + 0.65 * on), 2.4)
    im = Image.alpha_composite(im, glow(top, 3, 1.0))
    return finish(im, w, h)


# ---------------------------------------------------------------- level glow and arcs (act)
def ellipse_arcs(d, cx, cy, rx, ry, col, alpha, wid, segs=6, gap=0.5, rot=0.0):
    """A dashed elliptical ring: segs arcs separated by gaps."""
    span = 2 * math.pi / segs
    for k in range(segs):
        a0 = rot + k * span
        pts = []
        steps = 10
        for s in range(steps + 1):
            a = a0 + span * (1 - gap) * s / steps
            pts.append(((cx + rx * math.cos(a)) * SS, (cy + ry * math.sin(a)) * SS))
        d.line(pts, fill=rgba(col, alpha), width=int(wid * SS), joint="curve")


def act_frame(level, kind, w=190, h=150):
    """level 0..7. kind 'spk' (arcs go out), 'mic' (arcs come in), 'chip' (blue halo only, rounded square)."""
    im = new(w, h)
    if level == 0:
        return finish(im, w, h)
    p = level / 7.0
    col = {"spk": RED, "mic": MINT, "chip": BLUE}[kind]
    cx, cy = w / 2, h / 2
    base = new(w, h)
    bd = ImageDraw.Draw(base)
    if kind == "chip":
        s = 34 + 8 * p
        bd.rounded_rectangle([(cx - s) * SS, (cy - s * 0.8) * SS, (cx + s) * SS, (cy + s * 0.8) * SS],
                             radius=8 * SS, fill=rgba(col, 0.10 + 0.22 * p))
        base = base.filter(ImageFilter.GaussianBlur((10 + 6 * p) * SS))
        ring = new(w, h)
        rd = ImageDraw.Draw(ring)
        rd.rounded_rectangle([(cx - 33) * SS, (cy - 27) * SS, (cx + 33) * SS, (cy + 27) * SS],
                             radius=6 * SS, outline=rgba(col, 0.15 + 0.5 * p), width=int(1.8 * SS))
        im = Image.alpha_composite(base, glow(ring, 3, 1.0))
        return finish(im, w, h)
    rx0, ry0 = 50, 28                                           # capsule ellipse
    bd.ellipse([(cx - rx0) * SS, (cy - ry0) * SS, (cx + rx0) * SS, (cy + ry0) * SS], fill=rgba(col, 0.12 + 0.30 * p))
    base = base.filter(ImageFilter.GaussianBlur((9 + 7 * p) * SS))
    arcs = new(w, h)
    ad = ImageDraw.Draw(arcs)
    n_arcs = 1 + (level >= 3) + (level >= 5)
    for i in range(n_arcs):
        f = (i + 1) / 3.0
        # the whole set shifts with level so a changing signal makes the arcs travel
        shift = (p * 0.5) % 1.0
        pos = (f + (shift if kind == "spk" else -shift)) % 1.0
        grow = 1.15 + 0.7 * pos                                 # spk: pos rises = going out; mic: pos falls = coming in
        alpha = (0.25 + 0.6 * p) * (1.0 - 0.55 * pos if kind == "spk" else 0.45 + 0.5 * pos)
        ellipse_arcs(ad, cx, cy, rx0 * grow, ry0 * grow, col, alpha, 2.0, segs=6, gap=0.45,
                     rot=0.5 + (0.0 if kind == "spk" else 0.4))
    im = Image.alpha_composite(base, glow(arcs, 2, 0.9))
    return finish(im, w, h)


def main():
    os.makedirs(OUT, exist_ok=True)
    sizes = {}
    sizes["tone.png"] = stack([tone_frame(i / 32.0) for i in range(33)], "tone.png")
    sizes["phonic.png"] = stack([phonic_frame(i + 1) for i in range(15)], "phonic.png")
    sizes["baud.png"] = stack([baud_frame(i / 30.0) for i in range(31)], "baud.png")
    sizes["noise.png"] = stack([noise_frame(i / 32.0) for i in range(33)], "noise.png")
    for kind in ("spk", "mic", "chip"):
        name = "act_%s.png" % kind
        sizes[name] = stack([act_frame(i % 8, kind) for i in range(64)], name)
    old = os.path.join(OUT, "act.png")
    if os.path.exists(old):
        os.remove(old)                                           # the generic strip is no longer used
    for k, v in sizes.items():
        print(k, v)


if __name__ == "__main__":
    main()
