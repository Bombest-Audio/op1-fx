#!/usr/bin/env python3
"""Regenerate every image the Cwo MPC skin uses: images/bg.png and the six filmstrips.

Usage: python3 -I mpc/make_art.py   (needs Pillow, numpy, scipy)

Page coordinates: the art line sits at y=86, so a faceplate pixel (x, y) is at (x, y + 86).
Layout of the stage (faceplate pixels):
  freq track   cx 640, y 44        blue dot travels along a track (knob "freq", 33 frames)
  halo arc     cx 640, y 70-260    level axis pulses it, freq axis marches it (key "act", 64 frames)
  cow          cx 640, hooves 546  static, from design/art/cow-c.png
  delay taps   cx 640, y 548       13 echo ticks spread along the floor line (knob "delay", 33 frames)
  sideband     cx 322, y 350       diamond, upper half = upper sideband (knob "sideband", 33 frames)
  fdback bars  cx 985, y 316       10 bars fill from the bottom (knob "fdback", 33 frames)
  mix ticks    cx 940, y 62, beside the MIX knob     12 ticks fill from the left, dry to wet (knob "mix", 33 frames)
"""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi

HERE = Path(__file__).resolve().parent
IMG = HERE / "images"
ART = HERE.parent / "design" / "art" / "cow-c.png"
W, H = 1280, 630
PLATE = (0x07, 0x08, 0x0C)
BLUE, VIOLET, RED, MINT = (0x69, 0x8E, 0xFF), (0xA7, 0x7B, 0xFF), (0xFF, 0x4D, 0x5E), (0x2E, 0xF0, 0xA0)
TRACK = (0x23, 0x26, 0x2A)
SS = 3  # supersample factor for the strips


def rgba(c, a):
    return tuple(c) + (int(max(0, min(255, round(a)))),)


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


# ---- geometry shared by bg and strips (faceplate pixels) -------------------------------------------------
FREQ = dict(cx=640, cy=44, w=380, h=44, pad=22)
ARC = dict(cx=640, cy=165, w=700, h=190, ecx=350, ecy=188, rx=300, ry=150, n=37, pitch=5.0)
TAPS = dict(cx=640, cy=530, w=620, h=40, x0=30, span=560, base=34, n=13)
SIDE = dict(cx=322, cy=350, w=90, h=120)
MIXS = dict(cx=940, cy=62, w=120, h=22, n=12, tw=4, th=14, pitch=10)
CYAN = (0x4F, 0xD8, 0xFF)
BARS = dict(cx=985, cy=316, w=56, h=310, n=10, bw=40, bh=11, pitch=29)
FLOOR_Y = 548
COW_W, COW_BASE = 470, 546


def sideband_lines():
    """The diamond: 3 lines above the middle (upper sideband), 3 below (lower), widest at the middle."""
    cx, cy = SIDE["w"] / 2, SIDE["h"] / 2
    widths = [26, 50, 62]
    gap = 12
    up = [(cx, cy - 6 - i * gap, widths[2 - i]) for i in range(3)]      # nearest the middle first
    low = [(cx, cy + 6 + i * gap, widths[2 - i]) for i in range(3)]
    return up, low


def cow_layer():
    src = Image.open(ART).convert("RGB").resize((636, 636), Image.LANCZOS)
    a = np.asarray(src).astype(np.float32)
    lum = a.max(axis=2)
    lit = lum > 34
    lit = ndi.binary_closing(lit, structure=np.ones((3, 3)), iterations=6)
    lit = ndi.binary_fill_holes(lit)
    lit = ndi.binary_opening(lit, structure=np.ones((3, 3)), iterations=2)
    lab, n = ndi.label(lit)
    sizes = ndi.sum(lit, lab, range(1, n + 1))
    cow = ndi.binary_fill_holes(lab == (1 + int(np.argmax(sizes))))
    soft = ndi.gaussian_filter(cow.astype(np.float32), 0.9)
    alpha = np.clip(soft * 1.25 - 0.1, 0, 1)
    ys, xs = np.where(alpha > 0.5)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    rgb = a[y0:y1, x0:x1] * alpha[y0:y1, x0:x1, None]
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).convert("RGBA")
    al = Image.fromarray((alpha[y0:y1, x0:x1] * 255).astype(np.uint8))
    img.putalpha(al)
    s = COW_W / img.width
    return img.resize((COW_W, round(img.height * s)), Image.LANCZOS)


def make_bg():
    bg = Image.new("RGBA", (W, H), PLATE + (255,))
    cow = cow_layer()
    bg.alpha_composite(cow, (640 - cow.width // 2, COW_BASE - cow.height))
    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))  # static marks go on an overlay so their alpha blends
    d = ImageDraw.Draw(ov)
    # floor line and its left end tick
    x0, x1 = 640 - TAPS["w"] // 2 + TAPS["x0"], 640 - TAPS["w"] // 2 + TAPS["x0"] + TAPS["span"]
    d.line([(x0, FLOOR_Y), (x1, FLOOR_Y)], fill=rgba(VIOLET, 120), width=2)
    # freq track: line, end ticks, centre tick
    fx0 = FREQ["cx"] - FREQ["w"] // 2 + FREQ["pad"]
    fx1 = FREQ["cx"] + FREQ["w"] // 2 - FREQ["pad"]
    fy = FREQ["cy"]
    d.line([(fx0, fy), (fx1, fy)], fill=TRACK + (255,), width=3)
    for x, hh in ((fx0, 8), (fx1, 8), ((fx0 + fx1) // 2, 11)):
        d.line([(x, fy - hh), (x, fy + hh)], fill=(0x3A, 0x40, 0x46, 255), width=2)
    # fdback bar slots
    bx = BARS["cx"] - BARS["bw"] // 2
    for i in range(BARS["n"]):
        y = BARS["cy"] + BARS["h"] // 2 - 14 - i * BARS["pitch"]
        d.rectangle([bx, y, bx + BARS["bw"], y + BARS["bh"]], outline=rgba(RED, 60), width=1)
    # mix tick slots
    mx0 = MIXS["cx"] - MIXS["w"] // 2
    for i in range(MIXS["n"]):
        x = mx0 + 5 + i * MIXS["pitch"]
        y = MIXS["cy"] - MIXS["th"] // 2
        d.rectangle([x, y, x + MIXS["tw"], y + MIXS["th"]], outline=rgba(CYAN, 70), width=1)
    # sideband diamond, dim
    up, low = sideband_lines()
    ox, oy = SIDE["cx"] - SIDE["w"] // 2, SIDE["cy"] - SIDE["h"] // 2
    for cx, cy, w in up + low:
        d.line([(ox + cx - w / 2, oy + cy), (ox + cx + w / 2, oy + cy)], fill=rgba(MINT, 55), width=3)
    bg.alpha_composite(ov)
    IMG.mkdir(exist_ok=True)
    bg.convert("RGB").save(IMG / "bg.png")


def new_frame(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def finish(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def save_strip(frames, name):
    w, h = frames[0].size
    sheet = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * h))
    assert sheet.height <= 12288, (name, sheet.height)
    sheet.save(IMG / name)


def glow_dot(layer, x, y, r, col):
    g = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    ImageDraw.Draw(g).ellipse([(x - r * 2.2) * SS, (y - r * 2.2) * SS, (x + r * 2.2) * SS, (y + r * 2.2) * SS],
                              fill=rgba(col, 70))
    g = g.filter(ImageFilter.GaussianBlur(r * SS * 0.9))
    layer.alpha_composite(g)
    ImageDraw.Draw(layer).ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS], fill=rgba(col, 255))


def freq_strip(n=33):
    w, h, pad = FREQ["w"], FREQ["h"], FREQ["pad"]
    out = []
    for i in range(n):
        v = i / (n - 1)
        f = new_frame(w, h)
        d = ImageDraw.Draw(f)
        x = pad + v * (w - 2 * pad)
        y = h / 2
        if x > pad + 0.5:
            d.line([(pad * SS, y * SS), (x * SS, y * SS)], fill=rgba(BLUE, 235), width=int(3 * SS))
        glow_dot(f, x, y, 5.5, BLUE)
        out.append(finish(f, w, h))
    save_strip(out, "freq.png")


def taps_strip(n=33):
    w, h = TAPS["w"], TAPS["h"]
    out = []
    for i in range(n):
        v = i / (n - 1)
        f = new_frame(w, h)
        d = ImageDraw.Draw(f)
        sp = TAPS["span"] / (TAPS["n"] - 1) * (0.04 + 0.96 * v)
        for t in range(TAPS["n"]):
            x = TAPS["x0"] + t * sp
            hh = 15 if t >= 7 else 10
            fade = 1.0 - 0.35 * t / (TAPS["n"] - 1)
            d.line([(x * SS, TAPS["base"] * SS), (x * SS, (TAPS["base"] - hh) * SS)], fill=rgba(VIOLET, 255 * fade),
                   width=int(2.4 * SS))
        out.append(finish(f, w, h))
    save_strip(out, "delay.png")


def bars_strip(n=33):
    w, h = BARS["w"], BARS["h"]
    out = []
    for i in range(n):
        v = i / (n - 1)
        c = v * BARS["n"]
        f = new_frame(w, h)
        d = ImageDraw.Draw(f)
        bx = (w - BARS["bw"]) / 2
        for k in range(BARS["n"]):
            a = max(0.0, min(1.0, c - k))
            if a <= 0:
                continue
            y = h - 14 - k * BARS["pitch"]
            d.rectangle([bx * SS, y * SS, (bx + BARS["bw"]) * SS, (y + BARS["bh"]) * SS],
                        fill=rgba(RED, 60 + 195 * a))
        out.append(finish(f, w, h))
    save_strip(out, "fdback.png")


def mix_strip(n=33):
    w, h = MIXS["w"], MIXS["h"]
    out = []
    for i in range(n):
        c = i / (n - 1) * MIXS["n"]
        f = new_frame(w, h)
        d = ImageDraw.Draw(f)
        for k in range(MIXS["n"]):
            a = max(0.0, min(1.0, c - k))
            if a <= 0:
                continue
            x = 5 + k * MIXS["pitch"]
            y = (h - MIXS["th"]) / 2
            d.rectangle([x * SS, y * SS, (x + MIXS["tw"]) * SS, (y + MIXS["th"]) * SS], fill=rgba(CYAN, 70 + 185 * a))
        out.append(finish(f, w, h))
    save_strip(out, "mix.png")


def side_strip(n=33):
    w, h = SIDE["w"], SIDE["h"]
    up, low = sideband_lines()
    out = []
    for i in range(n):
        s = i / (n - 1)  # 1 = upper sideband only, 0 = lower only, 0.5 = ring modulation (both)
        f = new_frame(w, h)
        d = ImageDraw.Draw(f)
        for lines, amt in ((up, s), (low, 1 - s)):
            if amt <= 0.02:
                continue
            for cx, cy, lw in lines:
                d.line([((cx - lw / 2) * SS, cy * SS), ((cx + lw / 2) * SS, cy * SS)],
                       fill=rgba(MINT, 40 + 215 * smooth(amt * 1.0)), width=int(3.4 * SS))
        out.append(finish(f, w, h))
    save_strip(out, "sideband.png")


def arc_strip(ks=8, ls=8):
    w, h = ARC["w"], ARC["h"]
    cols = [VIOLET, MINT, RED, BLUE]
    out = []
    for k in range(ks):
        shift = k / ks * 4 * ARC["pitch"]  # one colour cycle (4 ticks) over the 8 knob steps, so it wraps cleanly
        for l in range(ls):
            lv = l / (ls - 1)
            f = new_frame(w, h)
            d = ImageDraw.Draw(f)
            if lv > 0:  # soft bloom under the ticks
                bloom = Image.new("RGBA", f.size, (0, 0, 0, 0))
                bd = ImageDraw.Draw(bloom)
                e = [(ARC["ecx"] - ARC["rx"] - 10) * SS, (ARC["ecy"] - ARC["ry"] - 10) * SS,
                     (ARC["ecx"] + ARC["rx"] + 10) * SS, (ARC["ecy"] + ARC["ry"] + 10) * SS]
                bd.arc(e, 180, 360, fill=rgba(BLUE, 60 * lv), width=int(16 * SS))
                f.alpha_composite(bloom.filter(ImageFilter.GaussianBlur(10 * SS)))
            for t in range(-4, ARC["n"] + 4):
                ang = 180.0 + t * ARC["pitch"] + shift  # 180 = left base, 360 = right base
                if ang < 180.0 or ang > 360.0:
                    continue
                edge = smooth(min(ang - 180.0, 360.0 - ang) / 14.0)
                th = math.radians(ang)
                c, s_ = math.cos(th), math.sin(th)
                # outward normal of the ellipse
                nx, ny = c / ARC["rx"], s_ / ARC["ry"]
                nn = math.hypot(nx, ny)
                nx, ny = nx / nn, ny / nn
                px, py = ARC["ecx"] + ARC["rx"] * c, ARC["ecy"] + ARC["ry"] * s_
                length = 8 + 18 * lv
                col = cols[t % 4]
                alpha = (46 + 200 * lv) * edge
                d.line([(px * SS, py * SS), ((px + nx * length) * SS, (py + ny * length) * SS)], fill=rgba(col, alpha),
                       width=int(2.6 * SS))
            out.append(finish(f, w, h))
    save_strip(out, "act.png")


if __name__ == "__main__":
    make_bg()
    freq_strip()
    taps_strip()
    bars_strip()
    mix_strip()
    side_strip()
    arc_strip()
    print("ok")
