#!/usr/bin/env python3
"""Regenerates every MPC animation strip for DBox. Run from anywhere: python3 make_art.py

Mapping, from the OP-1 screen asset display/dbox.svg (320x160, two oscillator rows):
  recovered  PITCH is a blue ring with a tick that turns (p1ring + p1line). WAVE shows a waveform glyph and a green
             selector box. DECAY is a white ring with a tick (w1ring + w1line). MODE is a pair of concentric rings
             with red ticks (modering, redring, funkline, filtline). The second oscillator row repeats the ring and
             glyph set in dim colours, the filter line sits in the MODE ring group.
  invented   Turn ranges (270 degree sweep), the sine to triangle to square morph for WAVE, the decaying bars inside
             the DECAY ring, 8 detent steps for MODE, the low-pass curve for FILTER and the whole signal-driven
             ripple on the drum head. The OP-1 only moves its ticks, it has no level animation.

Strips (all RGBA, transparent edges, one element each so the layers add up on the faceplate):
  act.png     64 frames 400x190, frame = PITCH step * 8 + level step. Orange glow on the drum head plus elliptical
              ripples around it. Level sets glow and ripple count, PITCH tightens the ripple spacing and moves the
              tint from blue to orange.
  pitch.png   33 frames 150x150, blue ring, tick turns -135..+135 degrees, arc grows from the 12 o'clock mark.
  wave.png    33 frames 150x150, green waveform morphing sine, triangle, square, three selector pips.
  decay.png   33 frames 150x150, white ring with a marker, bars inside whose tail gets longer.
  mode.png    33 frames 150x150, orange rings, 8 detents, tick turns in 45 degree steps (snaps, it is a switch).
  mix2.png, wave2.png, decay2.png, filter.png  same idea for the OSC2 tab (filter is a low-pass curve).
Frame height times frame count stays under the 12288 px strip limit.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
IMG = HERE / "images"
IMG.mkdir(exist_ok=True)
SS = 4  # supersampling for anti-aliased lines

BLUE, GREEN, WHITE, ORANGE = (105, 142, 255), (46, 240, 160), (233, 236, 232), (255, 154, 61)
RED = (255, 124, 112)
TEAL = (30, 190, 185)
RAIL = (108, 115, 112)

N = 33
FW = FH = 150
C = FW / 2


def new_layer(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def line(d, pts, col, a, w):
    d.line([(x * SS, y * SS) for x, y in pts], fill=col + (int(a),), width=max(1, int(w * SS)), joint="curve")


def dot(d, x, y, r, col, a=255):
    d.ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], fill=col + (int(a),))


def ring(d, cx, cy, r, col, a, w):
    d.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], outline=col + (int(a),), width=max(1, int(w * SS)))


def arc(d, cx, cy, r, a0, a1, col, a, w):
    """Arc between two clock angles in degrees (0 = 12 o'clock, clockwise)."""
    if a1 < a0:
        a0, a1 = a1, a0
    n = max(2, int((a1 - a0) / 3))
    pts = [pol(cx, cy, r, a0 + (a1 - a0) * i / n) for i in range(n + 1)]
    line(d, pts, col, a, w)


def pol(cx, cy, r, ang):
    t = math.radians(ang)
    return cx + r * math.sin(t), cy - r * math.cos(t)


def finish(layer, size, col, glow=1.0, blur=3.0):
    """Down-sample, add a soft glow of the same colour behind the lines."""
    sharp = layer.resize(size, Image.LANCZOS)
    g = sharp.filter(ImageFilter.GaussianBlur(blur))
    g.putalpha(g.getchannel("A").point(lambda v: min(255, int(v * 1.5 * glow))))
    return Image.alpha_composite(g, sharp)


def stack(frames, name):
    h = frames[0].height
    s = Image.new("RGBA", (frames[0].width, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        s.paste(f, (0, i * h))
    assert s.height <= 12288, (name, s.height)
    s.save(IMG / name, optimize=True)
    print(name, s.size, len(frames), "frames")


def knob_ring(t, col, bipolar=False, r=46):
    """Ring with a turning tick. t 0..1 maps to -135..+135 degrees."""
    L = new_layer(FW, FH)
    d = ImageDraw.Draw(L)
    ang = -135 + 270 * t
    ring(d, C, C, r, RAIL, 70, 1.4)
    for k in range(-3, 4):  # scale marks every 45 degrees
        a = k * 45
        x0, y0 = pol(C, C, r + 4, a)
        x1, y1 = pol(C, C, r + 9, a)
        line(d, [(x0, y0), (x1, y1)], RAIL, 110 if k == 0 else 60, 1.2)
    a_from = 0 if bipolar else -135
    arc(d, C, C, r, a_from, ang, col, 190, 2.8)
    x0, y0 = pol(C, C, 12, ang)
    x1, y1 = pol(C, C, r - 8, ang)
    line(d, [(x0, y0), (x1, y1)], col, 255, 2.4)
    ex, ey = pol(C, C, r, ang)
    dot(d, ex, ey, 4.6, col)
    dot(d, ex, ey, 2.0, (240, 244, 255))
    dot(d, C, C, 2.4, col, 200)
    return finish(L, (FW, FH), col)


def wave_shape(u, x):
    """Sine -> triangle -> square, u in 0..1, x in cycles."""
    s = math.sin(2 * math.pi * x)
    tri = 2 / math.pi * math.asin(s)
    sq = max(-1.0, min(1.0, s * 7))
    if u < 0.5:
        m = u / 0.5
        return (1 - m) * s + m * tri
    m = (u - 0.5) / 0.5
    return (1 - m) * tri + m * sq


def wave_frame(t, col):
    L = new_layer(FW, FH)
    d = ImageDraw.Draw(L)
    x0, x1, cy, amp = 16, FW - 16, 62, 26
    line(d, [(x0, cy), (x1, cy)], RAIL, 70, 1.1)
    pts = [(x0 + (x1 - x0) * i / 160, cy - amp * wave_shape(t, 2 * i / 160)) for i in range(161)]
    line(d, pts, col, 255, 2.4)
    # bracket ends, like the green selector box on the OP-1 screen
    for x, s in ((x0 - 6, 1), (x1 + 6, -1)):
        line(d, [(x + 5 * s, cy - 34), (x, cy - 34), (x, cy + 34), (x + 5 * s, cy + 34)], col, 120, 1.4)
    # three selector pips: sine, triangle, square
    for k, x in enumerate((45, 75, 105)):
        near = max(0.0, 1 - abs(t - k / 2) * 2.4)
        dot(d, x, 112, 3.0 + 2.0 * near, col, 70 + 185 * near)
    return finish(L, (FW, FH), col)


def decay_frame(t, col=WHITE):
    L = new_layer(FW, FH)
    d = ImageDraw.Draw(L)
    r = 46
    ang = -135 + 270 * t
    ring(d, C, C, r, RAIL, 70, 1.4)
    arc(d, C, C, r, -135, ang, col, 150, 2.4)
    ex, ey = pol(C, C, r, ang)
    dot(d, ex, ey, 4.6, col)
    tau = 1.1 + 9.0 * t * t
    n = 11
    x0, x1, base, hmax = 40, 110, 95, 46
    for i in range(n):
        x = x0 + (x1 - x0) * i / (n - 1)
        h = max(2.0, hmax * math.exp(-i / tau))
        line(d, [(x, base), (x, base - h)], col, 235 - 12 * i, 2.8)
    line(d, [(x0 - 4, base + 4), (x1 + 4, base + 4)], RAIL, 90, 1.2)
    return finish(L, (FW, FH), col)


def mode_frame(f):
    step = int(round(f / (N - 1) * 7))
    L = new_layer(FW, FH)
    d = ImageDraw.Draw(L)
    ro, ri = 56, 28
    ring(d, C, C, ro, RAIL, 90, 1.4)
    ring(d, C, C, ri, RAIL, 60, 1.4)
    for k in range(8):
        x, y = pol(C, C, ro, k * 45)
        on = k == step
        dot(d, x, y, 5.0 if on else 2.4, ORANGE, 255 if on else 110)
    ang = step * 45
    x0, y0 = pol(C, C, ri + 7, ang)
    x1, y1 = pol(C, C, ro - 7, ang)
    line(d, [(x0, y0), (x1, y1)], ORANGE, 255, 2.6)
    x0, y0 = pol(C, C, 7, ang)
    x1, y1 = pol(C, C, ri - 7, ang)
    line(d, [(x0, y0), (x1, y1)], RED, 230, 2.2)
    dot(d, C, C, 2.4, ORANGE, 200)
    return finish(L, (FW, FH), ORANGE)


def filter_frame(t, col=ORANGE):
    L = new_layer(FW, FH)
    d = ImageDraw.Draw(L)
    x0, x1, top, base = 16, FW - 16, 48, 100
    fc = 30 + t * 90
    q = 7
    pts = []
    for i in range(121):
        x = x0 + (x1 - x0) * i / 120
        if x <= fc:
            bump = q * math.exp(-((x - fc) / 8.0) ** 2)
            y = top - bump
        else:
            y = top + (x - fc) * 1.5 - q * math.exp(-((x - fc) / 8.0) ** 2)
        pts.append((x, min(base, y)))
    poly = pts + [(pts[-1][0], base), (pts[0][0], base)]
    d.polygon([(x * SS, y * SS) for x, y in poly], fill=col + (16,))
    line(d, [(x0, base + 3), (x1, base + 3)], RAIL, 90, 1.2)
    line(d, pts, col, 255, 2.4)
    line(d, [(fc, top - 10), (fc, base)], RED, 170, 1.4)
    dot(d, fc, top - q, 3.6, col)
    return finish(L, (FW, FH), col)


# signal-driven drum head activity
AW, AH = 400, 190
ACX, ACY = AW / 2, AH / 2
ASP = 0.45  # head ellipse aspect (height / width) in design/art/faceplate-bg


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def act_frame(k, l):
    lv = l / 7.0
    L = new_layer(AW, AH)
    if l == 0:
        return Image.new("RGBA", (AW, AH), (0, 0, 0, 0))
    tint = lerp(BLUE, ORANGE, 0.15 + 0.7 * (k / 7.0))
    # head glow: filled ellipse, blurred, orange
    G = Image.new("RGBA", (AW, AH), (0, 0, 0, 0))
    gd = ImageDraw.Draw(G)
    rx, ry = 78, 78 * ASP * 1.05
    gd.ellipse([ACX - rx, ACY - ry, ACX + rx, ACY + ry], fill=ORANGE + (int(255 * (0.10 + 0.30 * lv)),))
    G = G.filter(ImageFilter.GaussianBlur(10 + 5 * lv))
    d = ImageDraw.Draw(L)
    wl = 24 - 1.6 * k
    n = int(math.ceil(lv * 4))
    for i in range(n):
        r = 100 + i * wl
        a = (200 - 42 * i) * (0.45 + 0.55 * lv)
        w = max(1.0, 2.2 - 0.4 * i)
        d.ellipse([(ACX - r) * SS, (ACY - r * ASP) * SS, (ACX + r) * SS, (ACY + r * ASP) * SS],
                  outline=tint + (int(a),), width=int(w * SS))
    R = finish(L, (AW, AH), tint, glow=0.8, blur=2.6)
    return Image.alpha_composite(G, R)


def main():
    stack([knob_ring(i / (N - 1), BLUE, bipolar=True) for i in range(N)], "pitch.png")
    stack([wave_frame(i / (N - 1), GREEN) for i in range(N)], "wave.png")
    stack([decay_frame(i / (N - 1)) for i in range(N)], "decay.png")
    stack([mode_frame(i) for i in range(N)], "mode.png")
    stack([knob_ring(i / (N - 1), BLUE, bipolar=False) for i in range(N)], "mix2.png")
    stack([wave_frame(i / (N - 1), TEAL) for i in range(N)], "wave2.png")
    stack([decay_frame(i / (N - 1), (190, 196, 205)) for i in range(N)], "decay2.png")
    stack([filter_frame(i / (N - 1)) for i in range(N)], "filter.png")
    stack([act_frame(k, l) for k in range(8) for l in range(8)], "act.png")


if __name__ == "__main__":
    main()
