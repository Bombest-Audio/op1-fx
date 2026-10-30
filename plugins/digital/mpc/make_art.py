#!/usr/bin/env python3
"""Regenerates every image in mpc/images for the Digital MPC skin.

The OP-1 digital screen (firmware display/t10.svg, 320 x 160) is one violet line, from (249, 51) to (78, 76), and a
stack of coloured dots in the middle of the screen: blue (three copies), green, white and red. I redraw that in my own
style on the 1280 x 630 plate: the line runs from lower left (320, 432) to upper right (990, 172) as in the shared
faceplate, but the picture itself is not reused, so the stage is empty and the dots can move.

  bg.png        plate with a dim static line and a faint centre mark (replaces the shared faceplate art)
  feedback.png  blue   33 frames, the three blue echoes spread back along the line behind the head dot
  ratio.png     green  33 frames, the green dot travels up the line, a short lit segment follows it
  detune.png    white  33 frames, the white dot slides along a rail above the line (bipolar, centre is zero)
  crunch.png    orange 33 frames, glitch squares appear around the line, one more every few percent
  act.png       64 frames, top half of the signal glow    (frame = knob step * 8 + level step, knob axis unused)
  act2.png      64 frames, bottom half of the same glow   (a frame is capped at 12288 px / 64 = 192 px tall)

Run from anywhere: python3 make_art.py
"""
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")

PLATE = (7, 8, 12)
BLUE = (105, 142, 255)
BLUE_HI = (169, 188, 255)
VIOLET = (146, 86, 215)
VIOLET_DIM = (52, 34, 78)
GREEN = (46, 240, 160)
WHITE = (233, 236, 232)
ORANGE = (255, 154, 61)

SS = 3                       # supersampling
SW, SH = 700, 320            # stage in page pixels
STAGE_X, STAGE_Y = 305, 142  # stage top left on the 1280 x 630 plate (page y = plate y + 86)
A = (320 - STAGE_X, 432 - STAGE_Y)   # line start, lower left, in stage pixels
B = (990 - STAGE_X, 172 - STAGE_Y)   # line end, upper right
LEN = math.hypot(B[0] - A[0], B[1] - A[1])
D = ((B[0] - A[0]) / LEN, (B[1] - A[1]) / LEN)   # unit vector along the line
N = (-D[1], D[0])                                # unit normal, points down right
KNOB_FRAMES = 33
HALF = SH // 2


def on_line(t, off=0.0):
    """Stage point at fraction t along the line, offset by `off` px along the normal."""
    return (A[0] + (B[0] - A[0]) * t + N[0] * off, A[1] + (B[1] - A[1]) * t + N[1] * off)


C = on_line(0.5)


def layer():
    return Image.new("RGBA", (SW * SS, SH * SS), (0, 0, 0, 0))


def s(p):
    return (p[0] * SS, p[1] * SS)


def disc(d, p, r, col, a=255):
    x, y = s(p)
    r *= SS
    d.ellipse((x - r, y - r, x + r, y + r), fill=col + (int(a),))


def glow(img, blur):
    return img.filter(ImageFilter.GaussianBlur(blur * SS))


def finish(img, frame_w=SW, frame_h=SH):
    return img.resize((frame_w, frame_h), Image.LANCZOS)


def over(base, top):
    return Image.alpha_composite(base, top)


def strip(frames, name):
    h = frames[0].height
    assert h * len(frames) <= 12288, name
    out = Image.new("RGBA", (frames[0].width, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    out.save(os.path.join(OUT, name), optimize=True)
    print("wrote", name, out.size)


def soft_dot(p, r, col, halo=2.2, a=255):
    """Dot with a soft halo, as one supersampled layer."""
    base = layer()
    halo_l = layer()
    disc(ImageDraw.Draw(halo_l), p, r * halo, col, 90)
    base = over(base, glow(halo_l, r * 0.9))
    d = ImageDraw.Draw(base)
    disc(d, p, r, col, a)
    return base


def make_bg():
    img = Image.new("RGBA", (1280, 630), PLATE + (255,))
    st = layer()
    d = ImageDraw.Draw(st)
    d.line([s(A), s(B)], fill=VIOLET_DIM + (255,), width=2 * SS)
    for p in (A, B):
        disc(d, p, 3, VIOLET_DIM)
    for r in (13, 24):
        x, y = s(C)
        d.ellipse((x - r * SS, y - r * SS, x + r * SS, y + r * SS), outline=(35, 38, 42, 255), width=SS)
    img.alpha_composite(finish(st), (STAGE_X, STAGE_Y))
    img.convert("RGB").save(os.path.join(OUT, "bg.png"), optimize=True)
    print("wrote bg.png")


def make_feedback():
    frames = []
    for f in range(KNOB_FRAMES):
        k = f / (KNOB_FRAMES - 1)
        img = layer()
        # echoes behind the head, spread back down the line, fading
        for i, a in ((3, 0.4), (2, 0.65), (1, 0.9)):
            p = (C[0] - D[0] * k * 62 * i, C[1] - D[1] * k * 62 * i)
            img = over(img, soft_dot(p, 6.0 - 0.4 * i, BLUE, 2.0, 255 * a))
        if k > 0.02:   # faint connecting streak
            streak = layer()
            end = (C[0] - D[0] * k * 62 * 3, C[1] - D[1] * k * 62 * 3)
            ImageDraw.Draw(streak).line([s(C), s(end)], fill=BLUE + (int(70 * k),), width=3 * SS)
            img = over(glow(streak, 2), img)
        img = over(img, soft_dot(C, 7.5, BLUE_HI, 2.6))
        frames.append(finish(img))
    strip(frames, "feedback.png")


def make_ratio():
    frames = []
    for f in range(KNOB_FRAMES):
        k = f / (KNOB_FRAMES - 1)
        t = 0.5 + 0.40 * k
        p = on_line(t)
        img = layer()
        if k > 0.02:
            seg = layer()
            ImageDraw.Draw(seg).line([s(C), s(p)], fill=GREEN + (150,), width=3 * SS)
            img = over(img, glow(seg, 1.5))
            img = over(img, seg)
        img = over(img, soft_dot(p, 5.0, GREEN, 2.4))
        frames.append(finish(img))
    strip(frames, "ratio.png")


def make_detune():
    off = -17.0
    frames = []
    # static rail with a zero tick, then the dot on top
    rail = layer()
    rd = ImageDraw.Draw(rail)
    rd.line([s(on_line(0.1, off)), s(on_line(0.9, off))], fill=(233, 236, 232, 40), width=SS)
    for t, h in ((0.1, 4), (0.5, 7), (0.9, 4)):
        p = on_line(t, off)
        q1 = (p[0] - N[0] * h, p[1] - N[1] * h)
        q2 = (p[0] + N[0] * h, p[1] + N[1] * h)
        rd.line([s(q1), s(q2)], fill=(233, 236, 232, 90), width=SS)
    for f in range(KNOB_FRAMES):
        k = f / (KNOB_FRAMES - 1)
        t = 0.5 + (k - 0.5) * 0.8
        p = on_line(t, off)
        img = rail.copy()
        img = over(img, soft_dot(p, 4.6, WHITE, 2.3))
        frames.append(finish(img))
    strip(frames, "detune.png")


def make_crunch():
    rng = random.Random(1010)
    sq = []
    for _ in range(30):
        t = rng.uniform(0.18, 0.88)
        off = rng.choice((-1, 1)) * rng.uniform(12, 62)
        size = rng.choice((4, 6, 8, 10))
        alpha = rng.uniform(0.45, 1.0)
        sq.append((t, off, size, alpha))
    frames = []
    for f in range(KNOB_FRAMES):
        k = f / (KNOB_FRAMES - 1)
        n = int(round(k * len(sq)))
        img = layer()
        d = ImageDraw.Draw(img)
        for j in range(n):
            t, off, size, alpha = sq[j]
            p = on_line(t, off)
            x = round(p[0] / 2) * 2   # snap to a 2 px grid so it reads as digital
            y = round(p[1] / 2) * 2
            half = size / 2
            d.rectangle((s((x - half, y - half)), s((x + half, y + half))), fill=ORANGE + (int(255 * alpha),))
        img = over(glow(img, 2.0), img)
        frames.append(finish(img))
    strip(frames, "crunch.png")


def make_act():
    tops, bots = [], []
    cache = {}
    for lv in range(8):
        v = lv / 7
        img = layer()
        if lv:
            line = layer()
            ImageDraw.Draw(line).line([s(A), s(B)], fill=VIOLET + (int(70 + 120 * v),), width=int((2 + 4 * v) * SS))
            img = over(img, glow(line, 3 + 7 * v))
            core = layer()
            ImageDraw.Draw(core).line([s(A), s(B)], fill=VIOLET + (int(40 + 150 * v),), width=2 * SS)
            img = over(img, core)
            cl = layer()
            disc(ImageDraw.Draw(cl), C, 22 + 34 * v, BLUE, int(30 + 80 * v))
            img = over(img, glow(cl, 10 + 16 * v))
            rng = random.Random(77)
            for _ in range(lv // 2):   # violet beads sitting on the line, more of them when loud
                p = on_line(rng.uniform(0.12, 0.88))
                img = over(img, soft_dot(p, 3.0, VIOLET, 2.2, 200))
        full = finish(img)
        cache[lv] = (full.crop((0, 0, SW, HALF)), full.crop((0, HALF, SW, SH)))
    for knob in range(8):
        for lv in range(8):
            tops.append(cache[lv][0])
            bots.append(cache[lv][1])
    strip(tops, "act.png")
    strip(bots, "act2.png")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_bg()
    make_feedback()
    make_ratio()
    make_detune()
    make_crunch()
    make_act()
