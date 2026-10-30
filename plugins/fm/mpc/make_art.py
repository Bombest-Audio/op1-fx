#!/usr/bin/env python3
"""Regenerates every image in mpc/images for the Fm MPC skin.

The OP-1 FM screen draws four isometric wireframe cubes stepped down a diagonal (firmware display/fm.svg, group A:
steps of 47.6 x 23.8 px on a 320 x 160 screen). I redraw that cascade in my own style at 1.9x and split it into layers:

  bg.png        static plate with the dim cube skeleton (replaces the cubes in the shared faceplate)
  ratio.png     blue   33 frames, a dot travels down the cascade, cubes it passes light up
  index.png     green  33 frames, the two modulator cubes fill up like glass
  feedback.png  white  33 frames, an isometric orbit around each modulator sweeps open
  layer.png     orange 33 frames, the second pair (cubes 3 and 4) fades and glows in
  act.png       64 frames, top half of the signal glow  (frame = knob step * 8 + level step, knob axis unused)
  act2.png      64 frames, bottom half of the same glow (a frame is capped at 12288 px / 64 = 192 px tall)

Run from anywhere: python3 make_art.py
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")

PLATE = (7, 8, 12)
BLUE = (105, 142, 255)
BLUE_HI = (169, 188, 255)
MID = (63, 85, 153)
DARK = (38, 44, 92)
GREEN = (46, 240, 160)
WHITE = (233, 236, 232)
ORANGE = (255, 154, 61)

SS = 3                      # supersampling
SW, SH = 440, 340           # stage, in page pixels, centred on the page point (640, 410)
S = 1.9                     # firmware screen px to page px
DX, DY = 47.6 * S, 23.8 * S  # cascade step
HW = 27.75 * S              # cube half width
N_CUBES = 4
KNOB_FRAMES = 33
LAYER_Y0, LAYER_H = 10, 320   # knob layers are cropped out of the stage
ACT_H = SH // 2


def centre(i):
    """Front-top vertex of cube i (the firmware's local origin), in stage pixels."""
    top = -27.75 * S
    bottom = 3 * DY + 34.7 * S
    y0 = SH / 2 - (top + bottom) / 2
    x0 = SW / 2 - 1.5 * DX
    return (x0 + i * DX, y0 + i * DY)


def hexagon(c):
    x, y = c
    return [(x, y - 27.75 * S), (x + HW, y - 13.88 * S), (x + HW, y + 20.82 * S),
            (x, y + 34.7 * S), (x - HW, y + 20.82 * S), (x - HW, y - 13.88 * S)]


def body_centre(c):
    return (c[0], c[1] + 3.47 * S)


class Layer:
    """RGBA canvas at SS x, with a crisp pass and a glow pass."""

    def __init__(self):
        self.crisp = Image.new("RGBA", (SW * SS, SH * SS), (0, 0, 0, 0))
        self.glow = Image.new("RGBA", (SW * SS, SH * SS), (0, 0, 0, 0))
        self.dc = ImageDraw.Draw(self.crisp)
        self.dg = ImageDraw.Draw(self.glow)

    @staticmethod
    def _p(pts):
        return [(x * SS, y * SS) for x, y in pts]

    @staticmethod
    def _rgba(col, a):
        return (col[0], col[1], col[2], int(max(0.0, min(1.0, a)) * 255))

    def line(self, pts, col, a=1.0, w=1.6, glow=0.0, closed=False):
        if a <= 0.003:
            return
        p = self._p(pts + ([pts[0]] if closed else []))
        self.dc.line(p, fill=self._rgba(col, a), width=max(1, int(w * SS)), joint="curve")
        if glow > 0:
            self.dg.line(p, fill=self._rgba(col, a * glow), width=max(1, int(w * SS * 2.2)), joint="curve")

    def poly(self, pts, col, a):
        if a <= 0.003:
            return
        self.dc.polygon(self._p(pts), fill=self._rgba(col, a))

    def dot(self, c, r, col, a=1.0, glow=0.0):
        if a <= 0.003:
            return
        x, y = c[0] * SS, c[1] * SS
        self.dc.ellipse([x - r * SS, y - r * SS, x + r * SS, y + r * SS], fill=self._rgba(col, a))
        if glow > 0:
            g = r * 2.6
            self.dg.ellipse([x - g * SS, y - g * SS, x + g * SS, y + g * SS], fill=self._rgba(col, a * glow))

    def render(self, bloom=7.0):
        glow = self.glow.filter(ImageFilter.GaussianBlur(bloom * SS))
        # add the glow under the crisp pass
        out = Image.alpha_composite(glow, self.crisp)
        return out.resize((SW, SH), Image.LANCZOS)


# ---------------------------------------------------------------- pieces

def draw_skeleton(L, i, edge=MID, edge_a=0.75, inner=DARK, inner_a=1.0, w=1.6, glow=0.0):
    c = centre(i)
    x, y = c
    # hidden edges, then the visible Y, as in the firmware cube
    hid = [((x, y - 27.75 * S), (x, y - 2.8 * S)),
           ((x + 27.7 * S, y + 20.8 * S), (x + 2.7 * S, y + 8.3 * S)),
           ((x - 27.8 * S, y + 20.8 * S), (x - 2.8 * S, y + 8.3 * S))]
    vis = [((x, y + 34.7 * S), (x, y)), ((x, y), (x + HW, y - 13.88 * S)), ((x - HW, y - 13.88 * S), (x, y))]
    for a, b in hid + vis:
        L.line([a, b], inner, inner_a, w=1.2)
    L.line(hexagon(c), edge, edge_a, w=w, glow=glow, closed=True)


def draw_links(L, a=0.9, col=DARK):
    for i in range(N_CUBES - 1):
        p, q = body_centre(centre(i)), body_centre(centre(i + 1))
        L.line([p, q], col, a, w=1.2)


def lerp(a, b, t):
    return a + (b - a) * t


def lerp_col(a, b, t):
    return tuple(int(lerp(a[k], b[k], t)) for k in range(3))


def face_top(c):
    x, y = c
    return [(x - HW, y - 13.88 * S), (x, y - 27.75 * S), (x + HW, y - 13.88 * S), (x, y)]


# ---------------------------------------------------------------- static background

def make_bg():
    L = Layer()
    draw_links(L)
    for i in range(N_CUBES):
        draw_skeleton(L, i)
    stage = L.render(bloom=5.0)
    page = Image.new("RGBA", (1280, 630), PLATE + (255,))
    # stage centre sits on page (640, 410) which is faceplate (640, 324)
    page.alpha_composite(stage, (640 - SW // 2, 324 - SH // 2))
    return page.convert("RGB")


# ---------------------------------------------------------------- knob layers

def ratio_frame(t):
    """Blue dot travels along the link through the cube bodies; reached cubes light their outline."""
    L = Layer()
    pts = [body_centre(centre(i)) for i in range(N_CUBES)]
    pos = t * (N_CUBES - 1)
    # lit trail
    trail = [pts[0]]
    for i in range(1, N_CUBES):
        if pos >= i:
            trail.append(pts[i])
        else:
            f = pos - (i - 1)
            trail.append((lerp(pts[i - 1][0], pts[i][0], f), lerp(pts[i - 1][1], pts[i][1], f)))
            break
    if len(trail) > 1:
        L.line(trail, BLUE, 0.9, w=1.8, glow=0.5)
    for i in range(N_CUBES):
        near = max(0.0, 1.0 - abs(pos - i) / 0.8)
        reached = 0.35 if pos >= i - 1e-6 else 0.0
        a = max(near, reached)
        if a > 0:
            L.line(hexagon(centre(i)), BLUE, 0.35 + 0.65 * a, w=1.6 + 0.8 * near, glow=0.6 * a, closed=True)
    head = trail[-1]
    L.dot(head, 7.5, BLUE_HI, 1.0, glow=0.55)
    L.dot(head, 3.2, (255, 255, 255), 0.9)
    return L.render(bloom=6.0)


def fill_poly_h(c, h):
    """Isometric cross-section of cube c filled from the bottom up to fraction h (0..1)."""
    x, y = c
    z0 = y + 20.82 * S       # bottom of the side faces at the left/right corners
    top_z = y - 13.88 * S    # top of the side faces
    zb = lerp(z0, top_z, h)  # level of the left/right corners
    d = z0 - zb              # rise
    # left face, right face quads and the water surface rhombus
    left = [(x - HW, zb), (x, zb + 13.88 * S), (x, y + 34.7 * S), (x - HW, z0)]
    right = [(x + HW, zb), (x, zb + 13.88 * S), (x, y + 34.7 * S), (x + HW, z0)]
    surf = [(x - HW, zb), (x, zb - 13.88 * S), (x + HW, zb), (x, zb + 13.88 * S)]
    return left, right, surf


def index_frame(t):
    L = Layer()
    for i in (0, 2):
        c = centre(i)
        h = t
        if h > 0.01:
            left, right, surf = fill_poly_h(c, h)
            L.poly(left, GREEN, 0.20)
            L.poly(right, GREEN, 0.12)
            L.poly(surf, GREEN, 0.32)
            L.line(surf, GREEN, 0.95, w=1.6, glow=0.6, closed=True)
        L.line(hexagon(c), GREEN, 0.18 + 0.55 * t, w=1.5, glow=0.4 * t, closed=True)
    return L.render(bloom=6.0)


def orbit_points(c, sweep, steps=90):
    bx, by = body_centre(c)
    rx = 1.2 * HW
    ry = rx * 0.5
    pts = []
    for k in range(steps + 1):
        th = math.pi / 2 + sweep * k / steps   # start at the front of the ring
        pts.append((bx + rx * math.cos(th), by + ry * math.sin(th)))
    return pts


def feedback_frame(t):
    L = Layer()
    sweep = 2 * math.pi * 0.94 * t
    for i in (0, 2):
        c = centre(i)
        bx, by = body_centre(c)
        pts = orbit_points(c, max(sweep, 1e-3))
        # split into back (y above the body centre) and front runs so the back half reads as behind the cube
        run, back = [], None
        for p in pts:
            is_back = p[1] < by - 0.5
            if back is None or is_back == back:
                run.append(p)
            else:
                L.line(run, WHITE, 0.35 if back else 0.9, w=1.5, glow=0.25 if back else 0.5)
                run = [run[-1], p]
            back = is_back
        if len(run) > 1:
            L.line(run, WHITE, 0.35 if back else 0.9, w=1.5, glow=0.25 if back else 0.5)
        head = pts[-1]
        L.dot(head, 5.0, WHITE, 1.0, glow=0.5)
        # the ring start tick, so a closed ring still shows where it began
        L.dot(pts[0], 2.0, WHITE, 0.55 if t > 0.02 else 0.3)
    return L.render(bloom=5.5)


def layer_frame(t):
    L = Layer()
    for i in (2, 3):
        c = centre(i)
        draw_skeleton(L, i, edge=ORANGE, edge_a=0.12 + 0.88 * t, inner=ORANGE, inner_a=0.04 + 0.4 * t,
                      w=1.7, glow=0.7 * t)
        L.poly(face_top(c), ORANGE, 0.05 + 0.20 * t)
    if t > 0.01:
        a, b = body_centre(centre(2)), body_centre(centre(3))
        L.line([a, b], ORANGE, 0.15 + 0.7 * t, w=1.4, glow=0.4 * t)
    return L.render(bloom=7.0)


# ---------------------------------------------------------------- signal glow

def act_frame(level):
    """level step 0..7. Cubes light carrier-heavy (the last cube is the output) and the links wake up."""
    a = (level / 7.0) ** 0.85
    L = Layer()
    if level == 0:
        return L.render()
    for i in range(N_CUBES):
        s = min(1.0, a * (0.55 + 0.45 * i / (N_CUBES - 1)) * 1.1)
        c = centre(i)
        L.poly(hexagon(c), BLUE, 0.10 * s)
        L.line(hexagon(c), BLUE_HI, 0.25 + 0.7 * s, w=1.6 + 0.9 * s, glow=0.9 * s, closed=True)
    for i in range(N_CUBES - 1):
        s = min(1.0, a * 1.1)
        p, q = body_centre(centre(i)), body_centre(centre(i + 1))
        L.line([p, q], BLUE, 0.2 + 0.7 * s, w=1.3, glow=0.6 * s)
        m = (lerp(p[0], q[0], 0.5), lerp(p[1], q[1], 0.5))
        L.dot(m, 2.5 + 2.0 * s, BLUE_HI, 0.3 + 0.7 * s, glow=0.7)
    return L.render(bloom=9.0)


# ---------------------------------------------------------------- strips

def strip(frames, crop):
    x0, y0, x1, y1 = crop
    fw, fh = x1 - x0, y1 - y0
    out = Image.new("RGBA", (fw, fh * len(frames)), (0, 0, 0, 0))
    for k, f in enumerate(frames):
        out.paste(f.crop(crop), (0, k * fh))
    assert fh * len(frames) <= 12288, (fh, len(frames))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    make_bg().save(os.path.join(OUT, "bg.png"), optimize=True)
    crop = (0, LAYER_Y0, SW, LAYER_Y0 + LAYER_H)
    for name, fn in (("ratio", ratio_frame), ("index", index_frame), ("feedback", feedback_frame),
                     ("layer", layer_frame)):
        frames = [fn(k / (KNOB_FRAMES - 1)) for k in range(KNOB_FRAMES)]
        strip(frames, crop).save(os.path.join(OUT, name + ".png"), optimize=True)
    levels = [act_frame(l) for l in range(8)]
    frames = [levels[k % 8] for k in range(64)]     # frame = knob step * 8 + level step, knob axis ignored
    strip(frames, (0, 0, SW, ACT_H)).save(os.path.join(OUT, "act.png"), optimize=True)
    strip(frames, (0, ACT_H, SW, SH)).save(os.path.join(OUT, "act2.png"), optimize=True)


if __name__ == "__main__":
    main()
