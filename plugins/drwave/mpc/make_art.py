#!/usr/bin/env python3
"""Regenerates every MPC animation strip for DrWave. Run from anywhere: python3 make_art.py

The OP-1 drw.svg screen asset is an empty canvas, so none of this mapping is recovered from the firmware. It is my own:
  act.png     64 frames, 8 (WAVE step) x 8 (signal level). The bank line picked by WAVE lights up, level drives the glow.
  wave.png    33 frames, blue dot travelling along the eight bank slots.
  blend.png   33 frames, green trace morphing from the wave into its mirror image.
  motion.png  33 frames, white sweep curve that starts higher and settles onto the line.
  fold.png    33 frames, orange sine that folds over itself more as FOLD rises.
Frame height times frame count stays under the 12288 px strip limit.
"""
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
IMG = HERE / "images"
BG = HERE.parent / "design" / "art" / "faceplate-bg-1280x630.png"
SS = 3  # supersampling for anti-aliased lines

BLUE, GREEN, WHITE, ORANGE = (105, 142, 255), (46, 240, 160), (233, 236, 232), (255, 154, 61)
RAIL = (108, 115, 112)

ROW_W, ROW_H, N = 620, 40, 33


def new_layer():
    return Image.new("RGBA", (ROW_W * SS, ROW_H * SS), (0, 0, 0, 0))


def line(d, pts, col, a, w):
    d.line([(x * SS, y * SS) for x, y in pts], fill=col + (int(a),), width=max(1, int(w * SS)), joint="curve")


def dot(d, x, y, r, col, a=255):
    d.ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], fill=col + (int(a),))


def finish(layer, col, glow=1.0):
    """Down-sample, add a soft glow of the same colour behind the lines."""
    big = layer
    sharp = big.resize((ROW_W, ROW_H), Image.LANCZOS)
    g = sharp.filter(ImageFilter.GaussianBlur(3.2))
    g.putalpha(g.getchannel("A").point(lambda v: min(255, int(v * 1.6 * glow))))
    out = Image.alpha_composite(g, sharp)
    return out


def stack(frames, name):
    h = frames[0].height
    s = Image.new("RGBA", (frames[0].width, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        s.paste(f, (0, i * h))
    assert s.height <= 12288, (name, s.height)
    s.save(IMG / name, optimize=True)
    print(name, s.size, len(frames), "frames")


X0, X1 = 24, ROW_W - 24
CY = ROW_H / 2


def wave_frame(t):
    L = new_layer()
    d = ImageDraw.Draw(L)
    line(d, [(X0, CY), (X1, CY)], RAIL, 90, 1.2)
    xs = [X0 + i * (X1 - X0) / 7 for i in range(8)]
    px = X0 + t * (X1 - X0)
    for i, x in enumerate(xs):
        near = max(0.0, 1 - abs(px - x) / 40)
        line(d, [(x, CY - 5 - 4 * near), (x, CY + 5 + 4 * near)], BLUE, 90 + 150 * near, 1.4)
    line(d, [(X0, CY), (px, CY)], BLUE, 130, 2.4)
    dot(d, px, CY, 6, BLUE)
    dot(d, px, CY, 2.6, (233, 240, 255))
    return finish(L, BLUE)


def shape(u):
    """A lopsided single cycle (fundamental plus second harmonic) so its mirror image is visibly different."""
    return 0.62 * math.sin(2 * math.pi * u) + 0.38 * math.sin(4 * math.pi * u + 0.9)


def blend_frame(t):
    L = new_layer()
    d = ImageDraw.Draw(L)
    a, b = 170, 450
    line(d, [(X0, CY), (X1, CY)], RAIL, 70, 1.0)
    pts = []
    for k in range(0, 281):
        u = k / 280
        v = (1 - t) * shape(u * 2) + t * shape(2 - u * 2 + 0.0)
        pts.append((a + k, CY - v * 15))
    line(d, pts, GREEN, 235, 2.2)
    for x, v in ((X0 + 4, 1 - t), (X1 - 4, t)):
        dot(d, x, CY, 3 + 2.5 * v, GREEN, 70 + 185 * v)
    return finish(L, GREEN)


def motion_frame(t):
    L = new_layer()
    d = ImageDraw.Draw(L)
    line(d, [(X0, CY), (X1, CY)], RAIL, 90, 1.2)
    h = t * 17
    pts = []
    for k in range(0, 561):
        x = X0 + k
        pts.append((x, CY - h * math.exp(-k / 120.0)))
    line(d, pts, WHITE, 120 + 110 * t, 2.2)
    dot(d, X0, CY - h, 3 + 3 * t, WHITE, 120 + 135 * t)
    dot(d, X1, CY, 2.2, WHITE, 140)
    return finish(L, WHITE, 0.8)


def fold_frame(t):
    L = new_layer()
    d = ImageDraw.Draw(L)
    line(d, [(X0, CY), (X1, CY)], RAIL, 60, 1.0)
    gain = 1 + 5.0 * t * t
    pts = []
    for k in range(0, 561):
        u = k / 560
        w = math.sin(2 * math.pi * 3 * u)
        f = math.sin(gain * w * math.pi / 2 * 1.0)
        v = (1 - t) * w + t * f
        pts.append((X0 + k, CY - v * 15))
    line(d, pts, ORANGE, 235, 2.2)
    return finish(L, ORANGE)


def knob_strips():
    for name, fn in (("wave", wave_frame), ("blend", blend_frame), ("motion", motion_frame), ("fold", fold_frame)):
        stack([fn(i / (N - 1)) for i in range(N)], f"{name}.png")


AW, AH = 420, 190
ACX, ACY = 640, 252  # centre of the eight bank lines on the faceplate (faceplate coordinates)
LINES = [172, 194, 217, 241, 263, 286, 308, 331]


def act_strip():
    bg = Image.open(BG).convert("RGB")
    ox, oy = ACX - AW // 2, ACY - AH // 2
    crop = bg.crop((ox, oy, ox + AW, oy + AH))
    r, g, b = crop.split()
    # blue neon mask: blue clearly above red, bright enough. The mirror in front hides its own part of each line.
    px = crop.load()
    mk = Image.new("L", crop.size, 0)
    mp = mk.load()
    for y in range(crop.height):
        for x in range(crop.width):
            R, G, B = px[x, y]
            if B > 150 and B > R * 1.8 and B > G * 1.25:
                mp[x, y] = min(255, int((B - 100) * 1.6))
    bands = []
    for ly in LINES:
        band = Image.new("L", crop.size, 0)
        y0, y1 = ly - oy - 11, ly - oy + 11
        band.paste(mk.crop((0, y0, AW, y1)), (0, y0))
        bands.append(band)
    allmask = mk
    frames = []
    for ks in range(8):
        for ls in range(8):
            lev = (ls / 7.0) ** 0.8
            sel = bands[ks]
            sel_glow = sel.filter(ImageFilter.GaussianBlur(5))
            all_glow = allmask.filter(ImageFilter.GaussianBlur(7))
            f = Image.new("RGBA", (AW, AH), (0, 0, 0, 0))
            # soft halo from every line, grows with level
            halo = Image.new("RGBA", (AW, AH), BLUE + (0,))
            halo.putalpha(all_glow.point(lambda v, l=lev: int(min(255, v * 0.55 * l))))
            f = Image.alpha_composite(f, halo)
            # the line WAVE points at: white-blue core plus glow, brighter with level, a faint trace even when silent
            base = 0.22 + 0.78 * lev
            core = Image.new("RGBA", (AW, AH), (200, 220, 255, 0))
            core.putalpha(sel.point(lambda v, k=base: int(min(255, v * k))))
            glow = Image.new("RGBA", (AW, AH), BLUE + (0,))
            glow.putalpha(sel_glow.point(lambda v, k=base: int(min(255, v * 1.2 * k))))
            f = Image.alpha_composite(f, glow)
            f = Image.alpha_composite(f, core)
            frames.append(f)
    stack(frames, "act.png")


if __name__ == "__main__":
    IMG.mkdir(exist_ok=True)
    knob_strips()
    act_strip()
