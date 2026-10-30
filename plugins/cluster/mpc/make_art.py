#!/usr/bin/env python3
"""Regenerates every animated MPC image for Cluster. Run: python3 make_art.py

Layers (all RGBA, transparent, vertical filmstrips):
  osc.png   8 frames   OSCS      row of 8 dots, first N lit (OP-1: blue dot)
  fb.png    33 frames  FEEDBACK  echo arcs trailing left of the root, more and brighter with feedback
  spr.png   33 frames  SPREAD    two guide lines fanning from the root, angle follows spread
  wan.png   33 frames  WANDER    a thin drifting line, amplitude and phase follow wander
  act.png   64 frames  act       root glow; frame = oscs step * 8 + level step
                                 (level = glow size and brightness, oscs step = number of rays)
Page coordinates: faceplate pixel (x, y) is at (x, y + 86). Root of the strands is at faceplate (336, 320).
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
SS = 3  # supersampling

MINT = (0x2E, 0xF0, 0xA0)
VIOLET = (0xA7, 0x7B, 0xFF)
AMBER = (0xFF, 0xB3, 0x47)
CORAL = (0xFF, 0x4D, 0x5E)
TRACK = (0x23, 0x26, 0x2A)


def blank(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def glow(img, radius, gain=1.0):
    """Add a blurred copy of img under itself."""
    b = img.filter(ImageFilter.GaussianBlur(radius * SS))
    if gain != 1.0:
        r, g, bl, a = b.split()
        a = a.point(lambda v: min(255, int(v * gain)))
        b = Image.merge("RGBA", (r, g, bl, a))
    return Image.alpha_composite(b, img)


def finish(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def rgba(c, a):
    return (c[0], c[1], c[2], int(max(0, min(255, a))))


def save_strip(name, frames, w, h):
    assert w * 0 + h * len(frames) <= 12288, name
    out = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    out.save(os.path.join(OUT, name))
    print(name, out.size, len(frames), "frames")


def line(d, pts, color, width):
    d.line([(x * SS, y * SS) for x, y in pts], fill=color, width=max(1, int(width * SS)), joint="curve")


def dot(d, cx, cy, r, color):
    d.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], fill=color)


# ---------------------------------------------------------------- OSCS
def osc_strip():
    w, h, n = 300, 44, 8
    frames = []
    pitch = 34
    x0 = w / 2 - pitch * (n - 1) / 2
    for f in range(n):
        lit = f + 1
        base = blank(w, h)
        d = ImageDraw.Draw(base)
        for i in range(n):
            cx = x0 + i * pitch
            if i < lit:
                dot(d, cx, h / 2, 5.5, rgba(MINT, 255))
            else:
                dot(d, cx, h / 2, 4, rgba(TRACK, 255))
        # lit dots get a halo, dim dots stay flat
        lit_layer = blank(w, h)
        dl = ImageDraw.Draw(lit_layer)
        for i in range(lit):
            dot(dl, x0 + i * pitch, h / 2, 5.5, rgba(MINT, 255))
        halo = lit_layer.filter(ImageFilter.GaussianBlur(5 * SS))
        img = Image.alpha_composite(halo, base)
        # short bar under the last lit dot, like the OP-1 knob bar
        d2 = ImageDraw.Draw(img)
        cx = x0 + (lit - 1) * pitch
        line(d2, [(cx, h / 2 + 9), (cx, h / 2 + 14)], rgba(MINT, 200), 1.6)
        frames.append(finish(img, w, h))
    save_strip("osc.png", frames, w, h)


# ---------------------------------------------------------------- FEEDBACK
def fb_strip(n=33):
    w, h = 100, 120
    rx, ry = 96, 60  # root of the strands inside the frame (right side, vertical middle)
    frames = []
    for k in range(n):
        t = k / (n - 1)
        img = blank(w, h)
        d = ImageDraw.Draw(img)
        arcs = 1 + int(round(t * 4))  # 1..5 echoes
        for j in range(arcs):
            r = 12 + j * 16
            fade = (1.0 - j / 5.5) * (0.25 + 0.75 * t)
            a = 255 * fade if (t > 0.02 or j == 0) else 0
            if t < 0.02:
                a = 70  # at rest: a single faint arc
            half = 52 - j * 4  # degrees
            pts = []
            for s in range(-half, half + 1, 3):
                ang = math.radians(180 + s)
                pts.append((rx + r * math.cos(ang), ry + r * math.sin(ang)))
            line(d, pts, rgba(VIOLET, a), 1.8)
        img = glow(img, 3.5, 0.8)
        frames.append(finish(img, w, h))
    save_strip("fb.png", frames, w, h)


# ---------------------------------------------------------------- SPREAD
def spr_strip(n=33):
    w, h = 610, 360
    rx, ry = 6, 180
    length = 604
    frames = []
    for k in range(n):
        t = k / (n - 1)
        half = math.radians(1.5 + t * 17.0)  # strands reach about 15 degrees on the art at the default
        img = blank(w, h)
        d = ImageDraw.Draw(img)
        steps = 40
        for sgn in (-1, 1):
            for s in range(steps):
                a0, a1 = s / steps, (s + 1) / steps
                # dashed, fading towards the far end
                if s % 2:
                    continue
                p0 = (rx + length * a0 * math.cos(half), ry + sgn * length * a0 * math.sin(half))
                p1 = (rx + length * a1 * math.cos(half), ry + sgn * length * a1 * math.sin(half))
                fade = (1.0 - a0) ** 1.2
                line(d, [p0, p1], rgba(AMBER, 215 * fade * (0.5 + 0.5 * t)), 2.0)
        # end ticks that mark the opening
        for sgn in (-1, 1):
            x = rx + length * 0.97 * math.cos(half)
            y = ry + sgn * length * 0.97 * math.sin(half)
            dot(d, x, y, 2.6, rgba(AMBER, 200 * (0.4 + 0.6 * t)))
        img = glow(img, 2.5, 0.7)
        frames.append(finish(img, w, h))
    save_strip("spr.png", frames, w, h)


# ---------------------------------------------------------------- WANDER
def wan_strip(n=33):
    w, h = 500, 60
    frames = []
    for k in range(n):
        t = k / (n - 1)
        img = blank(w, h)
        d = ImageDraw.Draw(img)
        amp = 0.8 + t * 20
        pts = []
        for i in range(0, w + 1, 2):
            x = i
            env = math.sin(math.pi * i / w) ** 0.8  # pinned at both ends
            y = h / 2 + env * amp * (
                0.6 * math.sin(i / 38.0 + t * 7.0) + 0.4 * math.sin(i / 17.0 - t * 11.0 + 1.0)
            )
            pts.append((x, y))
        line(d, pts, rgba(CORAL, 150 + 90 * t), 1.8)
        # a small marker rides the line at a position that drifts with the knob
        mi = int(len(pts) * (0.15 + 0.7 * t))
        mx, my = pts[min(mi, len(pts) - 1)]
        dot(d, mx, my, 3.4, rgba(CORAL, 255))
        img = glow(img, 3, 0.8)
        frames.append(finish(img, w, h))
    save_strip("wan.png", frames, w, h)


# ---------------------------------------------------------------- act (signal)
def act_strip():
    w, h = 130, 130
    cx, cy = 45, 65  # root of the strands; left margin so the glow is not clipped
    frames = []
    for ks in range(8):  # oscs step
        for ls in range(8):  # level step
            lv = ls / 7.0
            img = blank(w, h)
            d = ImageDraw.Draw(img)
            # rays: one per oscillator, fanning right, longer and brighter with level
            n = ks + 1
            for i in range(n):
                frac = 0.0 if n == 1 else i / (n - 1) - 0.5
                ang = frac * math.radians(34)
                ln = 20 + 60 * lv
                a = 35 + 190 * lv
                line(d, [(cx, cy), (cx + ln * math.cos(ang), cy + ln * math.sin(ang))], rgba(MINT, a), 1.4)
            # glow disc
            r = 4 + 20 * lv
            core = blank(w, h)
            dc = ImageDraw.Draw(core)
            dot(dc, cx, cy, r * 0.45, rgba((0xE9, 0xFF, 0xF5), 60 + 195 * lv))
            dot(dc, cx, cy, r, rgba(MINT, 40 + 130 * lv))
            core = core.filter(ImageFilter.GaussianBlur((2 + 5 * lv) * SS))
            img = Image.alpha_composite(img, core)
            if ls == 0:
                # silent: keep only a tiny pilot dot, the faceplate art carries the rest
                img = blank(w, h)
                d0 = ImageDraw.Draw(img)
                dot(d0, cx, cy, 2.2, rgba(MINT, 120))
                img = img.filter(ImageFilter.GaussianBlur(1.2 * SS))
            frames.append(finish(img, w, h))
    save_strip("act.png", frames, w, h)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    osc_strip()
    fb_strip()
    spr_strip()
    wan_strip()
    act_strip()
