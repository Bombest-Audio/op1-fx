#!/usr/bin/env python3
"""Regenerates every image in plugins/sampler/mpc/images/ with Pillow. Run: python3 make_art.py

The faceplate is the hero waveform running top to bottom at x 640. The OP-1 sampler screen (display/sampler.svg) shows
the sample with four dots on dashed lines that slide along it, so the same idea is redrawn here:

  start_N.png  (N = 0..2, three stacked 176 px meters) blue marker, dashed line + ring that slides down the waveform with START (64 frames, top = 0)
  end_N.png    green marker on the right side, slides with END (64 frames, top = 0, default sits at the bottom)
  tune.png   white pitch ruler, ring rises and falls with TUNE, top = +12 st (33 frames)
  loop.png   orange loop ring, the arc sweeps round with LOOP, the value is the crossfade length (33 frames)
  act_N.png  signal pulse: the hero silhouette glows with output level (64 frames, frame = knob step * 8 + level step,
             only the level step is used, so the knob axis repeats the same 8 glows)

Everything is transparent at the edges and added on top of ../design/art/faceplate-bg-1280x630.png, which this script
only reads, never writes.
"""
import math
import os

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
BG = os.path.join(HERE, "..", "design", "art", "faceplate-bg-1280x630.png")

BLUE, GREEN, WHITE, ORANGE = (105, 142, 255), (46, 240, 160), (233, 236, 232), (255, 154, 61)
BLUE_HI = (169, 188, 255)
TRACK = (58, 63, 67)
SS = 4  # supersampling for the drawn markers


def clamp01(v):
    return max(0.0, min(1.0, v))


def lerp(a, b, t):
    return a + (b - a) * t


def layer_over(base, color, mask, opacity=1.0):
    """Composite a flat colour through an L mask. Blurring the mask (not the RGBA) keeps edges free of dark fringes."""
    solid = Image.new("RGBA", base.size, color + (0,))
    solid.putalpha(mask.point(lambda v: int(v * opacity)))
    base.alpha_composite(solid)


class Canvas:
    """One frame: draw on masks at SS times the size, then composite each mask (and a glow of it) with a colour."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))

    def mask(self):
        m = Image.new("L", self.img.size, 0)
        return m, ImageDraw.Draw(m)

    def put(self, mask, color, opacity=1.0, glow=0.0, glow_opacity=0.5):
        if glow > 0:
            g = mask.filter(ImageFilter.GaussianBlur(glow * SS))
            layer_over(self.img, color, g, glow_opacity)
        layer_over(self.img, color, mask, opacity)

    def done(self, size=None):
        return self.img.resize(size or (self.w, self.h), Image.LANCZOS)


def s(v):
    return v * SS


def ring(d, cx, cy, r, width):
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], outline=255, width=int(s(width)))


def disc(d, cx, cy, r):
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=255)


def hline_dashed(d, x0, x1, y, width, dash=4.0, gap=2.4):
    x = x0
    while x < x1:
        d.line([s(x), s(y), s(min(x1, x + dash)), s(y)], fill=255, width=int(s(width)))
        x += dash + gap


SEG_H = 176   # a meter strip is limited to 12288 px in total and the skin rebuilds it at display size, so a tall element
SEGS = 3      # is stacked from three 176 px meters (64 frames x 176 = 11264) and keeps every frame at 1:1


def save_segments(frames, base):
    """frames: full-height (SEGS * SEG_H) images. Writes <base>_0.png .. <base>_2.png, each the matching slice of every frame."""
    for j in range(SEGS):
        save_strip([f.crop((0, j * SEG_H, f.width, (j + 1) * SEG_H)) for f in frames], "%s_%d.png" % (base, j))


def save_strip(frames, name):
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * h))
    assert strip.size[1] <= 12288, (name, strip.size)
    strip.save(os.path.join(OUT, name))
    print("%-10s %dx%d  %d frames of %dx%d" % (name, strip.size[0], strip.size[1], len(frames), w, h))


# ---------------------------------------------------------------- START and END markers

MFW, MFH = 140, 370   # drawn at this size, delivered at 200 x 528 (3 stacked meters of 200 x 176)
M_Y0, M_Y1 = 14, 356  # travel of the marker centre (ring radius 6 plus glow stay inside), 20 to 508 at delivery size
MARKER_FRAMES = 64


def marker_frame(t, color, ring_x, fade_from_left):
    c = Canvas(MFW, MFH)
    y = lerp(M_Y0, M_Y1, t)
    # dashed line, as on the OP-1 (stroke 1.5, dash 4,2), brighter at the ring end
    base = Canvas(MFW, MFH)
    m, d = base.mask()
    hline_dashed(d, 4, MFW - 4, y, 1.3)
    grad = np.linspace(1.0, 0.2, m.size[0]) if fade_from_left else np.linspace(0.2, 1.0, m.size[0])
    arr = (np.asarray(m, dtype=np.float32) * grad[None, :]).astype(np.uint8)
    c.put(Image.fromarray(arr, "L"), color, opacity=0.85, glow=1.6, glow_opacity=0.35)
    # ring and dot, same proportions as the OP-1 marker (ring about twice the dot)
    m, d = c.mask()
    ring(d, ring_x, y, 6.2, 1.4)
    c.put(m, color, opacity=1.0, glow=3.0, glow_opacity=0.6)
    m, d = c.mask()
    disc(d, ring_x, y, 2.9)
    c.put(m, color, opacity=1.0)
    return c.done((200, SEGS * SEG_H))


def make_markers():
    n = MARKER_FRAMES
    save_segments([marker_frame(i / (n - 1), BLUE, 15, True) for i in range(n)], "start")
    save_segments([marker_frame(i / (n - 1), GREEN, MFW - 15, False) for i in range(n)], "end")


# ---------------------------------------------------------------- TUNE ruler

TFW, TFH = 63, 252    # drawn at this size, delivered at 84 x 336 (33 frames = 11088 px)
T_Y0, T_Y1 = 14, 238  # top = +12 st, bottom = -12 st, centre = 0
TUNE_FRAMES = 33


def tune_frame(t):
    c = Canvas(TFW, TFH)
    ax = 44  # the ruler axis
    ring_y = lerp(T_Y1, T_Y0, t)  # t = 1 is the top
    # axis line
    m, d = c.mask()
    d.line([s(ax), s(T_Y0 - 4), s(ax), s(T_Y1 + 4)], fill=255, width=int(s(1.2)))
    c.put(m, TRACK, opacity=1.0)
    # semitone ticks, longer at 3 st steps and the octaves, each lifts when the ring is near it
    for k in range(-12, 13):
        y = lerp(T_Y1, T_Y0, (k + 12) / 24.0)
        near = clamp01(1.0 - abs(y - ring_y) / 22.0)
        length = 12 if k % 12 == 0 else (7.5 if k % 3 == 0 else 4.2)
        m, d = c.mask()
        d.line([s(ax - length), s(y), s(ax - 2), s(y)], fill=255, width=int(s(1.2 if k % 12 else 1.8)))
        c.put(m, WHITE, opacity=lerp(0.28 if k % 3 else 0.42, 0.95, near), glow=1.0 if near > 0.3 else 0.0,
              glow_opacity=0.35 * near)
    # the ring
    m, d = c.mask()
    ring(d, ax, ring_y, 6.2, 1.4)
    c.put(m, WHITE, opacity=1.0, glow=3.0, glow_opacity=0.55)
    m, d = c.mask()
    disc(d, ax, ring_y, 2.9)
    c.put(m, WHITE, opacity=1.0)
    return c.done((84, 336))


def make_tune():
    n = TUNE_FRAMES
    save_strip([tune_frame(i / (n - 1)) for i in range(n)], "tune.png")


# ---------------------------------------------------------------- LOOP ring

LFW = LFH = 120       # 33 frames = 3960 px, shown 1:1
LOOP_FRAMES = 33


def loop_frame(t):
    c = Canvas(LFW, LFH)
    cx = cy = LFW / 2.0
    r = 38.0
    m, d = c.mask()
    ring(d, cx, cy, r, 2.6)
    c.put(m, TRACK, opacity=1.0)
    # the sweep starts at 12 o'clock and runs clockwise, its length is the crossfade length
    sweep = 360.0 * t
    if t > 0.004:
        m, d = c.mask()
        box = [s(cx - r), s(cy - r), s(cx + r), s(cy + r)]
        d.arc(box, -90, -90 + min(sweep, 359.5), fill=255, width=int(s(3.4)))
        # round caps
        for ang in (-90.0, -90.0 + min(sweep, 359.5)):
            px, py = cx + r * math.cos(math.radians(ang)), cy + r * math.sin(math.radians(ang))
            disc(d, px, py, 1.7)
        c.put(m, ORANGE, opacity=1.0, glow=3.5, glow_opacity=0.55)
        # arrowhead on the leading end, pointing along the sweep
        ang = math.radians(-90.0 + min(sweep, 359.5))
        px, py = cx + r * math.cos(ang), cy + r * math.sin(ang)
        tx, ty = -math.sin(ang), math.cos(ang)       # tangent, clockwise
        nx, ny = math.cos(ang), math.sin(ang)        # outward normal
        m, d = c.mask()
        tip = (px + tx * 7.5, py + ty * 7.5)
        a1 = (px + nx * 5.0, py + ny * 5.0)
        a2 = (px - nx * 5.0, py - ny * 5.0)
        d.polygon([(s(tip[0]), s(tip[1])), (s(a1[0]), s(a1[1])), (s(a2[0]), s(a2[1]))], fill=255)
        c.put(m, ORANGE, opacity=min(1.0, t * 12.0), glow=2.0, glow_opacity=0.4)
    else:
        # off: a dim cap at 12 o'clock so the ring still reads as the loop control
        m, d = c.mask()
        d.arc([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], -96, -84, fill=255, width=int(s(3.0)))
        c.put(m, ORANGE, opacity=0.35)
    # centre dot: dim when off, lit when looping
    m, d = c.mask()
    disc(d, cx, cy, 4.2)
    lit = clamp01(t * 14.0)
    c.put(m, ORANGE, opacity=lerp(0.22, 1.0, lit), glow=3.0 * lit, glow_opacity=0.5 * lit)
    return c.done()


def make_loop():
    n = LOOP_FRAMES
    save_strip([loop_frame(i / (n - 1)) for i in range(n)], "loop.png")


# ---------------------------------------------------------------- ACT pulse

AFW, AFH = 212, SEGS * SEG_H   # delivered as 3 stacked meters of 212 x 176
# crop of the faceplate that the act meters cover: x 534..746, y 10..538 (page y 96..624)
CROP = (534, 10, 746, 538)


def hero_mask():
    bg = Image.open(BG).convert("RGB").crop(CROP)
    a = np.asarray(bg, dtype=np.float32)
    # the hero is blue on a near black plate, use how far each pixel sits above the plate
    plate = np.array([7, 8, 12], dtype=np.float32)
    lum = np.clip((a - plate).max(axis=2) / 200.0, 0.0, 1.0)
    return Image.fromarray((lum * 255).astype(np.uint8), "L")


def act_frame(level_step, hero):
    d = level_step / 7.0
    full = hero.size
    if d <= 0:
        return Image.new("RGBA", full, (0, 0, 0, 0))
    # the silhouette swells and glows with level: a tight bloom and a wide halo, both scaled by level
    tight = hero.filter(ImageFilter.GaussianBlur(2.0 + 7.0 * d))
    wide = hero.filter(ImageFilter.GaussianBlur(10.0 + 9.0 * d))
    ta = np.asarray(tight, dtype=np.float32) / 255.0
    wa = np.asarray(wide, dtype=np.float32) / 255.0
    ha = np.asarray(hero, dtype=np.float32) / 255.0
    energy = (0.9 * ta + 1.0 * wa) * (d ** 1.4) + 0.18 * ha * d
    # fade the glow to nothing at the crop edges so the meter box never shows a hard rectangle
    hh, ww = energy.shape
    fy = np.clip(np.minimum(np.arange(hh), hh - 1 - np.arange(hh)) / 70.0, 0.0, 1.0)
    fx = np.clip(np.minimum(np.arange(ww), ww - 1 - np.arange(ww)) / 40.0, 0.0, 1.0)
    window = (np.sin(fy * math.pi / 2) ** 2)[:, None] * (np.sin(fx * math.pi / 2) ** 2)[None, :]
    alpha = np.clip(energy * 0.5 * window, 0.0, 0.5)
    mix = clamp01(d * 0.6)
    col = np.array([lerp(BLUE[i], BLUE_HI[i], mix) for i in range(3)], dtype=np.float32)
    rgba = np.zeros((full[1], full[0], 4), dtype=np.uint8)
    rgba[..., :3] = col.astype(np.uint8)
    rgba[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def make_act():
    hero = hero_mask()
    steps = [act_frame(i, hero) for i in range(8)]
    frames = [steps[i % 8] for i in range(64)]   # frame = knob step * 8 + level step
    save_segments(frames, "act")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_markers()
    make_tune()
    make_loop()
    make_act()
