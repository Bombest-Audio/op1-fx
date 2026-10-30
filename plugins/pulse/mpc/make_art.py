"""Regenerate every MPC skin image for Pulse: python3 make_art.py  (writes images/*.png next to this script).

The animation follows the OP-1 pulse screen (display/pls.svg), redrawn in my own neon style:
  blue   pulse train       SHAPE   resting pulse width, square to thin       (33 frame strip, images/shape.png)
  green  dashed span       SWEEP   depth of the width LFO, dots move apart   (images/sweep.png)
  white  twin brackets     RATE    spacing of the LFO period, tight = fast   (images/rate.png)
  orange stepped bars      DETUNE  oscillator spread, bipolar, flat at 0     (images/detune.png)
  cyan   bloom + core      level   signal glow on the train, by shape step   (images/act.png, 64 frames)
images/bg.png is the empty stage (replaces the neon picture for the MPC only), images/guard.png is a clear 1 frame
strip that keeps stray touches off the small meters.
Act frame = shape step (0..7) * 8 + level step (0..7), same as op1fx_act.h."""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
os.makedirs(OUT, exist_ok=True)

SS = 3  # supersampling for the line work
PLATE = (7, 8, 12)
BLUE = (77, 158, 255)
CYAN = (63, 210, 255)
GREEN = (0, 237, 149)
WHITE = (223, 217, 255)
ORANGE = (255, 138, 31)

# Stage layout in faceplate coordinates (page y = faceplate y + 86). All strips are 600 wide, centred on x 640.
SW = 600
ROWS = {  # name: (centre y in faceplate px, strip height)
    "train": (190, 150),
    "sweep": (312, 50),
    "rate": (384, 70),
    "detune": (462, 70),
}
FRAMES = 33


def lerp(a, b, t):
    return a + (b - a) * t


def blank(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def glow_line(img, pts, color, width=3.0, glow=1.0, dash=None, core=True):
    """Neon stroke: a wide soft bloom, a mid halo, then a bright thin core. Coordinates are final pixels."""
    w, h = img.size
    big = [(x * SS, y * SS) for x, y in pts]
    layers = ((width * 5.0, 7 * SS * glow, 0.55 * glow), (width * 2.2, 2.6 * SS, 0.8))
    for lw, blur, alpha in layers:
        lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        _stroke(d, big, color + (int(255 * min(1.0, alpha)),), lw * SS, dash)
        lay = lay.filter(ImageFilter.GaussianBlur(blur))
        img.alpha_composite(lay)
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    _stroke(d, big, color + (255,), width * SS, dash)
    img.alpha_composite(lay)
    if core:
        lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        light = tuple(int(c + (255 - c) * 0.6) for c in color)
        _stroke(d, big, light + (230,), max(1.0, width * 0.45) * SS, dash)
        img.alpha_composite(lay)


def _stroke(d, pts, fill, width, dash):
    if dash is None:
        d.line(pts, fill=fill, width=int(round(width)), joint="curve")
        r = width / 2.0
        for x, y in (pts[0], pts[-1]):
            d.ellipse((x - r, y - r, x + r, y + r), fill=fill)
        return
    on, off = dash[0] * SS, dash[1] * SS
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        length = math.hypot(x1 - x0, y1 - y0)
        if length == 0:
            continue
        ux, uy = (x1 - x0) / length, (y1 - y0) / length
        t = 0.0
        while t < length:
            e = min(length, t + on)
            d.line((x0 + ux * t, y0 + uy * t, x0 + ux * e, y0 + uy * e), fill=fill, width=int(round(width)))
            t += on + off


def glow_dot(img, cx, cy, r, color, glow=1.0):
    for rr, blur, alpha in ((r * 3.2, 6 * SS * glow, 0.5 * glow), (r * 1.6, 2.2 * SS, 0.8)):
        lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        d.ellipse(((cx - rr) * SS, (cy - rr) * SS, (cx + rr) * SS, (cy + rr) * SS),
                  fill=color + (int(255 * min(1.0, alpha)),))
        img.alpha_composite(lay.filter(ImageFilter.GaussianBlur(blur)))
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    d.ellipse(((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS), fill=color + (255,))
    img.alpha_composite(lay)


def finish(img, fade=44):
    """Down to final size, with the left and right ends faded so a strip never shows a hard cut."""
    w, h = img.size[0] // SS, img.size[1] // SS
    img = img.resize((w, h), Image.LANCZOS)
    if fade:
        ramp = Image.new("L", (w, 1), 255)
        for x in range(w):
            e = min(x, w - 1 - x) / float(fade)
            ramp.putpixel((x, 0), int(255 * max(0.0, min(1.0, e)) ** 1.5))
        mask = ramp.resize((w, h))
        a = img.getchannel("A")
        from PIL import ImageChops
        img.putalpha(ImageChops.multiply(a, mask))
    return img


def strip(frames):
    w, h = frames[0].size
    s = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        s.paste(f, (0, i * h))
    assert s.size[1] <= 12288, s.size
    return s


# ---------------------------------------------------------------- blue: pulse train (SHAPE)
TRAIN_H = ROWS["train"][1]
Y_HI, Y_LO = 46, 106
PERIOD = 104.0
TRAIN_X0 = 40.0


def duty(shape):
    """Pulse_duty_pct in the core: 50 percent for a square wave down to a thin pulse."""
    return lerp(0.5, 0.09, shape)


def train_points(shape, lfo=0.0):
    d = duty(shape)
    pts = [(0, Y_LO)]
    for i in range(5):
        x0 = TRAIN_X0 + i * PERIOD
        wi = d * PERIOD
        pts += [(x0, Y_LO), (x0, Y_HI), (x0 + wi, Y_HI), (x0 + wi, Y_LO)]
    pts.append((SW, Y_LO))
    return pts


def make_shape():
    frames = []
    for f in range(FRAMES):
        img = blank(SW, TRAIN_H)
        glow_line(img, train_points(f / (FRAMES - 1.0)), BLUE, width=3.4)
        frames.append(finish(img))
    return strip(frames)


# ---------------------------------------------------------------- cyan: signal bloom on the train (act)
def make_act():
    frames = []
    for ks in range(8):
        shape = ks / 7.0
        for ls in range(8):
            img = blank(SW, TRAIN_H)
            lv = ls / 7.0
            if ls > 0:
                pts = train_points(round(shape * (FRAMES - 1)) / (FRAMES - 1.0))
                # bloom: a wide halo that widens with level, then a brighter core line over the blue one
                for lw, blur, alpha in ((16 + 18 * lv, 9 + 9 * lv, 0.20 + 0.55 * lv), (7 + 4 * lv, 3.5, 0.18 + 0.5 * lv)):
                    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
                    d = ImageDraw.Draw(lay)
                    _stroke(d, [(x * SS, y * SS) for x, y in pts], CYAN + (int(255 * min(1.0, alpha)),), lw * SS, None)
                    img.alpha_composite(lay.filter(ImageFilter.GaussianBlur(blur * SS)))
                lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
                d = ImageDraw.Draw(lay)
                _stroke(d, [(x * SS, y * SS) for x, y in pts], (190, 240, 255, int(40 + 190 * lv)), 2.2 * SS, None)
                img.alpha_composite(lay)
                # wobble: loud notes shake the high edges up, small sparks ride the tops of the pulses
                if ls >= 3:
                    n = ls - 2
                    d = Image.new("RGBA", img.size, (0, 0, 0, 0))
                    dd = ImageDraw.Draw(d)
                    dduty = duty(shape)
                    for i in range(6):
                        x0 = TRAIN_X0 + i * PERIOD
                        sx = (x0 + dduty * PERIOD * (0.25 + 0.5 * ((i * 37 + ks * 11) % 10) / 10.0)) * SS
                        if sx > SW * SS - 40 * SS:
                            continue
                        h = (4 + 3 * n) * SS
                        dd.line((sx, (Y_HI - 2) * SS, sx, (Y_HI - 2) * SS - h), fill=(215, 245, 255, 150), width=2 * SS)
                    img.alpha_composite(d.filter(ImageFilter.GaussianBlur(1.2 * SS)))
            frames.append(finish(img))
    return strip(frames)


# ---------------------------------------------------------------- green: dashed span (SWEEP)
SWEEP_H = ROWS["sweep"][1]


def make_sweep():
    frames = []
    cy = SWEEP_H / 2.0
    for f in range(FRAMES):
        s = f / (FRAMES - 1.0)
        img = blank(SW, SWEEP_H)
        half = lerp(6, 250, s)
        cx = SW / 2.0
        a, b = cx - half, cx + half
        if half > 10:
            glow_line(img, [(a, cy), (b, cy)], GREEN, width=1.8, glow=0.6, dash=(5, 4), core=False)
        # end brackets, as on the OP-1 (a short upright with a foot)
        for x in (a, b):
            glow_line(img, [(x, cy - 10), (x, cy + 10)], GREEN, width=2.2, glow=0.7)
        glow_dot(img, a, cy, 4.2, GREEN, glow=0.9)
        glow_dot(img, b, cy, 4.2, GREEN, glow=0.9)
        frames.append(finish(img, fade=30))
    return strip(frames)


# ---------------------------------------------------------------- white: twin brackets (RATE)
RATE_H = ROWS["rate"][1]


def bracket(img, x, top, bot, w=11):
    glow_line(img, [(x - w / 2, bot), (x - w / 2, top), (x + w / 2, top), (x + w / 2, bot)], WHITE, width=2.2, glow=0.7)


def make_rate():
    frames = []
    top, bot = 8, RATE_H - 10
    for f in range(FRAMES):
        r = f / (FRAMES - 1.0)
        img = blank(SW, RATE_H)
        gap = lerp(300, 34, r)  # long period at slow rates, tight at fast ones
        cx = SW / 2.0
        xa, xb = cx - gap / 2.0, cx + gap / 2.0
        ymid = (top + bot) / 2.0 + 8
        glow_line(img, [(xa, ymid), (xb, ymid)], (0, 170, 170), width=1.6, glow=0.5, dash=(4, 4), core=False)
        glow_dot(img, xa, ymid, 3.4, (0, 190, 190), glow=0.7)
        glow_dot(img, xb, ymid, 3.4, (0, 190, 190), glow=0.7)
        bracket(img, xa, top, bot)
        bracket(img, xb, top, bot)
        frames.append(finish(img, fade=30))
    return strip(frames)


# ---------------------------------------------------------------- orange: stepped bars (DETUNE)
DET_H = ROWS["detune"][1]
STEPS = [0.45, 0.75, 1.0, 0.65, 0.9, 0.35]


def make_detune():
    frames = []
    cy = DET_H / 2.0
    bw = 52.0
    x0 = SW / 2.0 - bw * len(STEPS) / 2.0
    for f in range(FRAMES):
        v = (f / (FRAMES - 1.0)) * 2.0 - 1.0  # -1..1, frame 16 is exactly 0
        img = blank(SW, DET_H)
        glow_line(img, [(x0 - 14, cy), (x0 + bw * len(STEPS) + 14, cy)], (140, 80, 40), width=1.6, glow=0.4, core=False)
        pts = [(x0, cy)]
        for i, k in enumerate(STEPS):
            y = cy - v * k * (DET_H / 2.0 - 8)
            pts += [(x0 + i * bw, y), (x0 + (i + 1) * bw, y)]
        pts.append((x0 + bw * len(STEPS), cy))
        glow_line(img, pts, ORANGE, width=2.4, glow=0.9)
        frames.append(finish(img, fade=30))
    return strip(frames)


# ---------------------------------------------------------------- background and guard
def make_bg():
    W, H = 1280, 630
    bg = Image.new("RGB", (W, H), PLATE)
    # faint ambient glow behind the train
    amb = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(amb)
    d.ellipse((330, 70, 950, 330), fill=(6, 20, 32))
    amb = amb.filter(ImageFilter.GaussianBlur(70))
    from PIL import ImageChops
    bg = ImageChops.add(bg, amb)

    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    # OP-1 style grid (#002740 on the device), kept very quiet
    gx0, gx1, gy0, gy1 = 330, 950, 96, 268
    grid = (0, 39, 64, 120)
    for i in range(10):
        x = gx0 + (gx1 - gx0) * i / 9.0
        d.line((x, gy0, x, gy1), fill=grid, width=1)
    for j in range(5):
        y = gy0 + (gy1 - gy0) * j / 4.0
        d.line((gx0, y, gx1, y), fill=grid, width=1)
    # row frames and tracks
    track = (35, 38, 42, 255)
    for name, (cy, h) in ROWS.items():
        if name == "train":
            continue
        d.line((340, cy, 940, cy), fill=(24, 27, 31, 255), width=1)
        d.line((340, cy - h / 2.0 + 4, 340, cy + h / 2.0 - 4), fill=track, width=1)
        d.line((940, cy - h / 2.0 + 4, 940, cy + h / 2.0 - 4), fill=track, width=1)
    d.line((330, ROWS["train"][0] + 80, 950, ROWS["train"][0] + 80), fill=(24, 27, 31, 255), width=1)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/SFNSMono.ttf", 12)
    except OSError:
        font = ImageFont.load_default()
    labels = (("train", "SHAPE", BLUE), ("sweep", "SWEEP", GREEN), ("rate", "RATE", WHITE), ("detune", "DETUNE", ORANGE))
    for name, text, col in labels:
        cy, h = ROWS[name]
        y = cy - h / 2.0 + (14 if name == "train" else 2)
        d.text((346, y), text, font=font, fill=tuple(int(c * 0.45) for c in col) + (255,))
    bg = bg.convert("RGBA")
    bg.alpha_composite(ov)
    return bg.convert("RGB")


def main():
    jobs = {
        "shape.png": make_shape,
        "act.png": make_act,
        "sweep.png": make_sweep,
        "rate.png": make_rate,
        "detune.png": make_detune,
    }
    for name, fn in jobs.items():
        im = fn()
        im.save(os.path.join(OUT, name), optimize=True)
        print("wrote", name, im.size)
    make_bg().save(os.path.join(OUT, "bg.png"), optimize=True)
    print("wrote bg.png")
    Image.new("RGBA", (SW, 216), (0, 0, 0, 0)).save(os.path.join(OUT, "guard.png"))
    print("wrote guard.png")


if __name__ == "__main__":
    main()
