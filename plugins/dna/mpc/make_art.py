#!/usr/bin/env python3
"""Regenerates every image in mpc/images/ for the DNA MPC skin.

bg.png   the faceplate plus empty wells for the animated parts (the JUCE art is untouched)
mut.png  MUTATE  blue    the 128 genes as a 16 x 8 grid, 33 frames, mutate -1..1
bits.png BITS    green   the cycle window over the 128 genes, 33 frames, bits 0..1
sub.png  SUB     orange  the two-cycle wave table, second cycle morphing, 33 frames
grit.png GRIT    white   random bit errors along a row, 33 frames
act.png  signal  64 frames, frame = mutate step * 8 + level step (the engine writes it)

The gene logic repeats core/dna_core.c (same xorshift seed, same per-gene thresholds) so the grid shows the real genome.
Run from anywhere: python3 make_art.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
BG_SRC = os.path.join(HERE, "..", "design", "art", "faceplate-bg-1280x630.png")

BLUE = (47, 139, 255)
GREEN = (29, 233, 160)
ORANGE = (255, 138, 31)
WHITE = (233, 236, 232)
WELL = (35, 38, 42)      # theme_line
WELL_DIM = (22, 24, 28)
SS = 3                   # supersampling

# stage geometry in faceplate pixels (page y is this + 86)
LCX, RCX = 410, 870
GRID_CY, ROW_CY = 250, 345
GRID_W, GRID_H = 288, 144
ROW_H = 22
COLS, ROWS = 16, 8
PITCH = 18
CELL = 13


# ---- the genome, same as core/dna_core.c genome_make() ----
def genome():
    s = 0x2545F491
    def xs():
        nonlocal s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        return s
    g = [[(xs() >> 11) & 1 for _ in range(128)] for _ in range(3)]
    order = [(xs() >> 8) / 16777216.0 for _ in range(128)]
    return g, order


G, ORDER = genome()
RAMP = 6.0


def smooth(x):
    return x * x * (3 - 2 * x)


def gene_values(m):
    """gene values -1..1 and transition amount x per gene for mutate m, as gene_update() does."""
    mm = abs(m)
    mut = 1 if m < 0 else 2
    vals, trans = [], []
    for k in range(128):
        t = 0.5 / RAMP + ORDER[k] * (1 - 1 / RAMP)
        x = smooth(min(1.0, max(0.0, (mm - t) * RAMP + 0.5)))
        a = 1.0 if G[0][k] else -1.0
        b = 1.0 if G[mut][k] else -1.0
        vals.append(a + (b - a) * x)
        trans.append(x * (1 - x) * 4 if a != b else 0.0)
    return vals, trans


def mix(c, d, t):
    return tuple(int(round(c[i] + (d[i] - c[i]) * t)) for i in range(3))


def font(size):
    for p in ("/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Monaco.ttf",
              "/System/Library/Fonts/Supplemental/Courier New.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def layer(w, h):
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def done(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def rr(d, box, r, fill=None, outline=None, width=1):
    x0, y0, x1, y1 = [v * SS for v in box]
    d.rounded_rectangle((x0, y0, x1, y1), radius=r * SS, fill=fill, outline=outline, width=width * SS)


def strip(frames, w, h):
    out = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    assert h * len(frames) <= 12288, "strip too tall"
    return out


# ---- MUTATE: the genome grid ----
def cell_box(k):
    c, r = k % COLS, k // COLS
    x0 = (GRID_W - COLS * PITCH) / 2 + c * PITCH + (PITCH - CELL) / 2
    y0 = (GRID_H - ROWS * PITCH) / 2 + r * PITCH + (PITCH - CELL) / 2
    return (x0, y0, x0 + CELL, y0 + CELL)


def mut_frame(i, n=33):
    m = -1 + 2 * i / (n - 1)
    vals, trans = gene_values(m)
    im, d = layer(GRID_W, GRID_H)
    glow, gd = layer(GRID_W, GRID_H)
    for k in range(128):
        a = (vals[k] + 1) / 2          # 0 = off, 1 = on
        t = trans[k]
        col = mix(BLUE, WHITE, 0.75 * t)
        if a > 0.02:
            rr(d, cell_box(k), 3, fill=col + (int(255 * (0.15 + 0.85 * a)),))
            rr(gd, cell_box(k), 3, fill=col + (int(150 * a),))
    glow = glow.filter(ImageFilter.GaussianBlur(5 * SS))
    glow.alpha_composite(im)
    return done(glow, GRID_W, GRID_H)


# ---- BITS: how many genes make one cycle, 2 + 126 * v ----
def bits_frame(i, n=33):
    v = i / (n - 1)
    nb = 2 + 126 * v
    im, d = layer(GRID_W, ROW_H)
    glow, gd = layer(GRID_W, ROW_H)
    seg = GRID_W / 128.0
    for k in range(128):
        on = min(1.0, max(0.0, nb - k))      # partial last gene
        if on <= 0:
            continue
        x0 = k * seg + 0.5
        x1 = (k + 1) * seg - 0.5
        rr(d, (x0, 8, x1, 16), 1, fill=GREEN + (int(255 * (0.25 + 0.75 * on)),))
        rr(gd, (x0, 6, x1, 18), 1, fill=GREEN + (int(120 * on),))
    # the cursor at the end of the window
    cx = nb * seg
    rr(d, (cx - 1, 3, cx + 1, 21), 1, fill=mix(GREEN, WHITE, 0.6) + (255,))
    glow = glow.filter(ImageFilter.GaussianBlur(3 * SS))
    glow.alpha_composite(im)
    return done(glow, GRID_W, ROW_H)


# ---- SUB: the two-cycle table, second cycle negated and rotated by 43 of 128 slots ----
STEPS = 36  # steps per cycle, 36 * 2 * 4 px = 288


def cycle1():
    return [1.0 if G[0][int(k * 128 / STEPS)] else -1.0 for k in range(STEPS)]


def sub_frame(i, n=33):
    s = i / (n - 1)
    c1 = cycle1()
    rot = int(round(43 * STEPS / 128.0))
    c2 = [(1 - s) * c1[k] + s * (-c1[(k - rot) % STEPS]) for k in range(STEPS)]
    im, d = layer(GRID_W, GRID_H)
    glow, gd = layer(GRID_W, GRID_H)
    mid = GRID_H / 2
    amp = 40
    px = GRID_W / (STEPS * 2)
    for half, cyc, strong in ((0, c1, False), (1, c2, True)):
        for k in range(STEPS):
            v = cyc[k]
            x0 = (half * STEPS + k) * px + 0.6
            x1 = x0 + px - 1.2
            y0, y1 = (mid - amp * v, mid) if v >= 0 else (mid, mid - amp * v)
            if abs(v) < 0.04:
                y0, y1 = mid - 1, mid + 1
            alpha = 255 if strong else 110
            col = ORANGE if strong else mix(ORANGE, WELL, 0.45)
            rr(d, (x0, min(y0, y1), x1, max(y0, y1)), 1, fill=col + (alpha,))
            if strong:
                rr(gd, (x0, min(y0, y1), x1, max(y0, y1)), 1, fill=ORANGE + (130,))
    glow = glow.filter(ImageFilter.GaussianBlur(4 * SS))
    glow.alpha_composite(im)
    return done(glow, GRID_W, GRID_H)


# ---- GRIT: bit errors, a gene errs with probability 0.5 * grit; the row shows them at 2x scale ----
GRIT_CELLS = 36
_u = []
_s = 0x1B873593
for _ in range(GRIT_CELLS):
    _s ^= (_s << 13) & 0xFFFFFFFF
    _s ^= _s >> 17
    _s ^= (_s << 5) & 0xFFFFFFFF
    _u.append((_s >> 8) / 16777216.0)


def grit_frame(i, n=33):
    g = i / (n - 1)
    im, d = layer(GRID_W, ROW_H)
    glow, gd = layer(GRID_W, ROW_H)
    pitch = GRID_W / GRIT_CELLS
    for k in range(GRIT_CELLS):
        if _u[k] < g:
            x0 = k * pitch + 1
            x1 = (k + 1) * pitch - 1
            col = mix(ORANGE, WHITE, 0.3)
            rr(d, (x0, 4, x1, 18), 2, fill=col + (255,))
            rr(gd, (x0, 3, x1, 19), 2, fill=ORANGE + (140,))
    glow = glow.filter(ImageFilter.GaussianBlur(3 * SS))
    glow.alpha_composite(im)
    return done(glow, GRID_W, ROW_H)


# ---- act: signal level pulse around the helix, knob axis changes the ripple spacing and sparks ----
AW, AH = 300, 180


def hash01(a, b):
    x = (a * 374761393 + b * 668265263) & 0xFFFFFFFF
    x = (x ^ (x >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((x ^ (x >> 16)) & 0xFFFFFF) / 16777216.0


def act_frame(f):
    knob, lvl = divmod(f, 8)
    if lvl == 0:
        return Image.new("RGBA", (AW, AH), (0, 0, 0, 0))
    L = lvl / 7.0
    im, d = layer(AW, AH)
    cx, cy = AW / 2, AH / 2
    # strand-coloured bloom: blue to the left, green to the right, wider and brighter with level
    bloom = Image.new("RGBA", (AW * SS, AH * SS), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bloom)
    rx, ry = (30 + 120 * L) * SS, (22 + 62 * L) * SS
    bd.ellipse((cx * SS - rx, cy * SS - ry, cx * SS + rx, cy * SS + ry), fill=(255, 255, 255, int(90 * L)))
    bloom = bloom.filter(ImageFilter.GaussianBlur(26 * SS))
    tint = Image.new("RGBA", bloom.size, (0, 0, 0, 0))
    td = ImageDraw.Draw(tint)
    td.rectangle((0, 0, cx * SS, AH * SS), fill=BLUE + (255,))
    td.rectangle((cx * SS, 0, AW * SS, AH * SS), fill=GREEN + (255,))
    # fade to zero before the frame edge so no rectangle shows
    mask = Image.new("L", bloom.size, 0)
    ImageDraw.Draw(mask).ellipse((22 * SS, 14 * SS, (AW - 22) * SS, (AH - 14) * SS), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(9 * SS))
    from PIL import ImageChops
    tint.putalpha(ImageChops.multiply(bloom.getchannel("A"), mask))
    im.alpha_composite(tint)
    # ripples leaving the helix, spacing set by the mutate step
    spacing = 14 + knob * 2
    for r in range(3):
        rad = (18 + r * spacing + L * 60)
        a = int(200 * L * (1 - r / 3.5) * max(0.0, 1 - rad / 140))
        if a <= 4:
            continue
        col = mix(BLUE, GREEN, r / 2.0)
        d.ellipse((( cx - rad) * SS, (cy - rad * 0.45) * SS, (cx + rad) * SS, (cy + rad * 0.45) * SS),
                  outline=col + (a,), width=max(1, int(1.5 * SS)))
    # sparks, more of them at higher level, positions fixed by the knob step
    nsp = int(2 + 12 * L)
    for k in range(nsp):
        ang = hash01(knob + 1, k) * 2 * math.pi
        rad = 35 + hash01(k, knob + 7) * (40 + 80 * L)
        x = cx + math.cos(ang) * rad
        y = cy + math.sin(ang) * rad * 0.45
        col = (BLUE, GREEN, ORANGE, WHITE)[k % 4]
        r = 1.6 + 1.2 * L
        d.ellipse(((x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS), fill=col + (int(120 + 135 * L),))
    return done(im, AW, AH)


# ---- bg: faceplate plus empty wells ----
def make_bg():
    bg = Image.open(BG_SRC).convert("RGBA")
    big = bg.resize((1280 * 2, 630 * 2), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    f = font(20)

    def well(cx, cy, w, h, r=10):
        d.rounded_rectangle(((cx - w / 2) * 2, (cy - h / 2) * 2, (cx + w / 2) * 2, (cy + h / 2) * 2),
                            radius=r * 2, fill=(11, 12, 16, 255), outline=WELL + (255,), width=2)

    def caption(cx, cy, text, col):
        w = d.textlength(text, font=f)
        d.text((cx * 2 - w / 2, cy * 2 - 10), text, font=f, fill=col + (255,))

    # genome grid well and its dim cells
    well(LCX, GRID_CY, GRID_W + 24, GRID_H + 24)
    for k in range(128):
        x0, y0, x1, y1 = cell_box(k)
        ox, oy = LCX - GRID_W / 2, GRID_CY - GRID_H / 2
        d.rounded_rectangle(((ox + x0) * 2, (oy + y0) * 2, (ox + x1) * 2, (oy + y1) * 2), radius=6,
                            outline=WELL + (255,), width=2)
    well(LCX, ROW_CY, GRID_W + 24, ROW_H + 12, 8)
    caption(LCX, GRID_CY - GRID_H / 2 - 28, "GENOME  128 GENES", mix(BLUE, WELL, 0.4))
    caption(LCX, ROW_CY + 28, "CYCLE LENGTH", mix(GREEN, WELL, 0.4))
    # wave well: baseline between the two cycles and a divider at the cycle boundary
    well(RCX, GRID_CY, GRID_W + 24, GRID_H + 24)
    ox, oy = RCX - GRID_W / 2, GRID_CY - GRID_H / 2
    d.line((ox * 2, (oy + GRID_H / 2) * 2, (ox + GRID_W) * 2, (oy + GRID_H / 2) * 2), fill=WELL + (255,), width=2)
    d.line(((ox + GRID_W / 2) * 2, (oy + 8) * 2, (ox + GRID_W / 2) * 2, (oy + GRID_H - 8) * 2),
           fill=WELL + (255,), width=2)
    well(RCX, ROW_CY, GRID_W + 24, ROW_H + 12, 8)
    pitch = GRID_W / GRIT_CELLS
    for k in range(GRIT_CELLS):
        x0 = ox + k * pitch + 1
        d.rounded_rectangle((x0 * 2, (ROW_CY - 7) * 2, (x0 + pitch - 2) * 2, (ROW_CY + 7) * 2), radius=4,
                            outline=WELL + (255,), width=2)
    caption(RCX, GRID_CY - GRID_H / 2 - 28, "TWO CYCLE TABLE", mix(ORANGE, WELL, 0.4))
    caption(RCX, ROW_CY + 28, "BIT ERRORS", mix(ORANGE, WELL, 0.4))
    return big.resize((1280, 630), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    make_bg().convert("RGB").save(os.path.join(OUT, "bg.png"), optimize=True)
    strip([mut_frame(i) for i in range(33)], GRID_W, GRID_H).save(os.path.join(OUT, "mut.png"), optimize=True)
    strip([bits_frame(i) for i in range(33)], GRID_W, ROW_H).save(os.path.join(OUT, "bits.png"), optimize=True)
    strip([sub_frame(i) for i in range(33)], GRID_W, GRID_H).save(os.path.join(OUT, "sub.png"), optimize=True)
    strip([grit_frame(i) for i in range(33)], GRID_W, ROW_H).save(os.path.join(OUT, "grit.png"), optimize=True)
    strip([act_frame(f) for f in range(64)], AW, AH).save(os.path.join(OUT, "act.png"), optimize=True)


if __name__ == "__main__":
    main()
