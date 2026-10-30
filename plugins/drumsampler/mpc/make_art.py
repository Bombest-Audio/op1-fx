#!/usr/bin/env python3
"""Regenerates every MPC animation image for DrumSampler. Run from anywhere: python3 make_art.py

Mapping, from the OP-1 drum2.svg screen (colours and element ids recovered, motion invented):
  act.png    64 frames, 8 (SLOT step) x 8 (signal level). The sample waveform (teal in drum2.svg). The slot picks one of
             eight drum-like shapes, the level brightens, glows and swells it. Mapping is mine.
  start.png  33 frames, green loop-in dot (loopin) sliding along a track under the waveform. Recovered: green, dot + ring
             + dashed bar. Motion with START is the natural one, invented in detail.
  end.png    33 frames, white loop-out dot (loopout) sliding along a track above the waveform. Same notes.
  pitch.png  33 frames, blue dot on a vertical semitone ladder (pitch group in drum2.svg is blue). Ladder is mine.
  mode.png   3 frames, orange play-mode glyph: gate, one-shot, loop (drum2.svg pm group has these three, red there).
  bg.png     the shared faceplate dimmed a little so the waveform reads over the record. The JUCE art is untouched.
Frame height times frame count stays under the 12288 px strip limit.
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

HERE = Path(__file__).resolve().parent
IMG = HERE / "images"
BG = HERE.parent / "design" / "art" / "faceplate-bg-1280x630.png"
SS = 3

BLUE, GREEN, WHITE, ORANGE = (105, 142, 255), (0, 237, 149), (233, 236, 232), (255, 154, 61)
TEAL_DIM, TEAL_HOT = (38, 118, 140), (150, 232, 255)
RAIL = (108, 115, 112)

W, WAVE_H, ROW_H = 560, 150, 36
X0, X1 = 20, W - 20


def layer(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def line(d, pts, col, a, w):
    d.line([(x * SS, y * SS) for x, y in pts], fill=col + (int(a),), width=max(1, int(w * SS)), joint="curve")


def dot(d, x, y, r, col, a=255, fill=True, ring=0.0):
    if fill:
        d.ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], fill=col + (int(a),))
    if ring:
        R = r + ring
        d.ellipse([(x - R) * SS, (y - R) * SS, (x + R) * SS, (y + R) * SS], outline=col + (int(a * 0.8),), width=SS)


def finish(img, size, blur=3.0, glow=1.0):
    sharp = img.resize(size, Image.LANCZOS)
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


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


# ---- waveform shapes, one per slot step -------------------------------------------------------------------------
def shape(step):
    """Envelope samples (0..1) across the stage: kick, snare, hat, clap, tom, rim, shaker, crash flavours."""
    rnd = random.Random(40 + step)
    n = 130
    decay = [2.6, 3.4, 7.0, 4.2, 2.2, 9.0, 3.0, 1.3][step]
    noise = [0.12, 0.55, 0.8, 0.65, 0.18, 0.3, 0.75, 0.7][step]
    pulses = [1, 1, 1, 3, 1, 1, 4, 1][step]
    out = []
    for i in range(n):
        t = i / (n - 1)
        e = 0.0
        for p in range(pulses):
            tp = t - p * (0.12 if step == 3 else 0.2)
            if tp >= 0:
                e = max(e, math.exp(-decay * tp * (1.0 if pulses == 1 else 2.4)))
        e *= 1 - noise + noise * rnd.random() * 1.6
        out.append(min(1.0, e * (0.95 if step != 0 else 1.0)))
    sm = [sum(out[max(0, i - 1): i + 2]) / len(out[max(0, i - 1): i + 2]) for i in range(n)]
    sm[0] = max(sm[0], 0.2)
    return sm


SHAPES = [shape(s) for s in range(8)]


def act_frame(step, lvl):
    k = lvl / 7.0
    env = SHAPES[step]
    cy = WAVE_H / 2
    amp = (WAVE_H / 2 - 10) * (0.82 + 0.18 * k)
    n = len(env)
    xs = [X0 + (X1 - X0) * i / (n - 1) for i in range(n)]
    top = [(x, cy - 2 - amp * e) for x, e in zip(xs, env)]
    bot = [(x, cy + 2 + amp * e) for x, e in zip(xs, env)]
    col = lerp(TEAL_DIM, TEAL_HOT, k)
    a = 140 + 115 * k
    im = layer(W, WAVE_H)
    d = ImageDraw.Draw(im)
    line(d, [(X0, cy), (X1, cy)], col, a * 0.45, 1.0)
    line(d, top, col, a, 1.6)
    line(d, bot, col, a, 1.6)
    # a few inner strokes so it reads as a waveform body, thinner and fainter
    for e0 in (0.5,):
        line(d, [(x, cy - amp * e * e0) for x, e in zip(xs, env)], col, a * 0.35, 1.0)
        line(d, [(x, cy + amp * e * e0) for x, e in zip(xs, env)], col, a * 0.35, 1.0)
    return finish(im, (W, WAVE_H), blur=2.0 + 3.0 * k, glow=0.5 + 1.3 * k)


def marker_frame(v, col, up):
    """Row strip. up=True: dot track sits at the bottom of the strip and the dashed stub points up to the wave."""
    im = layer(W, ROW_H)
    d = ImageDraw.Draw(im)
    ty = ROW_H - 10 if up else 10
    line(d, [(X0, ty), (X1, ty)], RAIL, 70, 1.0)
    for q in range(5):
        x = X0 + (X1 - X0) * q / 4
        line(d, [(x, ty - 3), (x, ty + 3)], RAIL, 90, 1.0)
    x = X0 + (X1 - X0) * v
    # dashed stub toward the waveform, like the dashed bar in drum2.svg
    y_far = 2 if up else ROW_H - 2
    n = 3
    for s in range(n):
        ya = ty + (y_far - ty) * (s * 2) / (n * 2)
        yb = ty + (y_far - ty) * (s * 2 + 1) / (n * 2)
        line(d, [(x, ya), (x, yb)], col, 150, 1.5)
    dot(d, x, ty, 3.2, col, 255, ring=2.6)
    return finish(im, (W, ROW_H), blur=2.4)


PW, PH = 60, 170


def pitch_frame(v):
    im = layer(PW, PH)
    d = ImageDraw.Draw(im)
    x = PW / 2 + 6
    y0, y1 = 18, PH - 18
    line(d, [(x, y0), (x, y1)], RAIL, 80, 1.0)
    for q in range(13):  # 12 semitones, octave marks longer
        y = y1 - (y1 - y0) * q / 12
        L = 9 if q in (0, 6, 12) else 4
        line(d, [(x - L, y), (x, y)], RAIL, 140 if L == 9 else 90, 1.0)
    # plus and minus like the pitch glyph
    line(d, [(PW / 2 - 18, 6), (PW / 2 - 8, 6)], BLUE, 150, 1.5)
    line(d, [(PW / 2 - 13, 1), (PW / 2 - 13, 11)], BLUE, 150, 1.5)
    line(d, [(PW / 2 - 18, PH - 6), (PW / 2 - 8, PH - 6)], BLUE, 150, 1.5)
    y = y1 - (y1 - y0) * v
    line(d, [(x, y), (x + 14, y)], BLUE, 120, 1.5)
    dot(d, x, y, 3.6, BLUE, 255, ring=3.0)
    return finish(im, (PW, PH), blur=2.4)


MW = 90


def mode_frame(m):
    im = layer(MW, MW)
    d = ImageDraw.Draw(im)
    cy = MW / 2
    c = ORANGE
    tri = lambda x: [(x * SS, (cy - 9) * SS), ((x + 15) * SS, cy * SS), (x * SS, (cy + 9) * SS)]
    if m == 0:  # gate: line into a triangle
        line(d, [(14, cy), (56, cy)], c, 255, 1.8)
        d.polygon(tri(56), fill=c + (255,))
    elif m == 1:  # one-shot: line, triangle, end bar
        line(d, [(14, cy), (48, cy)], c, 255, 1.8)
        d.polygon(tri(48), fill=c + (255,))
        line(d, [(68, cy - 10), (68, cy + 10)], c, 255, 1.8)
    else:  # loop: rounded track with an arrow head
        box = [(18, cy - 12), (72, cy + 12)]
        d.rounded_rectangle([box[0][0] * SS, box[0][1] * SS, box[1][0] * SS, box[1][1] * SS], radius=12 * SS,
                            outline=c + (255,), width=int(1.8 * SS))
        d.rectangle([40 * SS, (cy - 14) * SS, 56 * SS, (cy - 10) * SS], fill=(0, 0, 0, 0))
        d.polygon([(56 * SS, (cy - 17) * SS), (47 * SS, (cy - 12) * SS), (56 * SS, (cy - 7) * SS)], fill=c + (255,))
    return finish(im, (MW, MW), blur=2.6)


def main():
    IMG.mkdir(exist_ok=True)
    stack([act_frame(s, l) for s in range(8) for l in range(8)], "act.png")
    stack([marker_frame(i / 32, GREEN, True) for i in range(33)], "start.png")
    stack([marker_frame(i / 32, WHITE, False) for i in range(33)], "end.png")
    stack([pitch_frame(i / 32) for i in range(33)], "pitch.png")
    stack([mode_frame(m) for m in range(3)], "mode.png")
    bg = Image.open(BG).convert("RGB")
    ImageEnhance.Brightness(bg).enhance(0.7).save(IMG / "bg.png", optimize=True)
    print("bg.png", bg.size)


if __name__ == "__main__":
    main()
