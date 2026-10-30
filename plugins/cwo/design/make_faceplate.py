#!/usr/bin/env python3
"""Build design/art/faceplate-bg-1280x630.png from the Higgsfield cow render (design/art/cow-c.png).

The PNG carries a mask in its alpha channel: opaque on the cow (and the floor line), transparent everywhere else.
The plugin editor paints the plate color, then the live halo, then this image on top, so the halo sits behind the cow.
Where alpha is 0 the RGB is the plate color plus a dim static halo, so a host that ignores alpha (the MPC skin
renderer) still shows a complete faceplate.

Usage: python3 -I design/make_faceplate.py   (needs Pillow, numpy, scipy)
"""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi

HERE = Path(__file__).resolve().parent
W, H = 1280, 630
PLATE = (0x07, 0x08, 0x0C)
ART = 636          # the cow render is scaled to ART x ART
ART_X, ART_Y = 322, -14
# Halo geometry in faceplate pixels. The editor draws the live halo with the same numbers.
HALO_CX, HALO_CY, HALO_R0, HALO_R1 = 506.0, 214.0, 150.0, 190.0
FLOOR_Y = 528

src = Image.open(HERE / "art" / "cow-c.png").convert("RGB").resize((ART, ART), Image.LANCZOS)
a = np.asarray(src).astype(np.float32)
lum = a.max(axis=2)

# Cow silhouette: anything clearly brighter than the backdrop, closed over the black patches and filled.
lit = lum > 34
lit = ndi.binary_closing(lit, structure=np.ones((3, 3)), iterations=6)
lit = ndi.binary_fill_holes(lit)
lit = ndi.binary_opening(lit, structure=np.ones((3, 3)), iterations=2)
lab, n = ndi.label(lit)
sizes = ndi.sum(lit, lab, range(1, n + 1))
cow = lab == (1 + int(np.argmax(sizes)))
cow = ndi.binary_fill_holes(cow)
soft = ndi.gaussian_filter(cow.astype(np.float32), 0.9)
alpha_art = np.clip(soft * 1.25 - 0.1, 0, 1)

canvas = np.zeros((H, W, 3), np.float32)
canvas[:] = PLATE
alpha = np.zeros((H, W), np.float32)

# cow pixels (backdrop of the render blended to the plate color at the edge)
y0, y1 = max(ART_Y, 0), min(ART_Y + ART, H)
sy0 = y0 - ART_Y
sub = a[sy0:sy0 + (y1 - y0)]
sub_alpha = alpha_art[sy0:sy0 + (y1 - y0)]
rgb = sub * sub_alpha[..., None] + np.array(PLATE, np.float32) * (1 - sub_alpha[..., None])
canvas[y0:y1, ART_X:ART_X + ART] = rgb
alpha[y0:y1, ART_X:ART_X + ART] = sub_alpha

# floor line, drawn into both the colors and the alpha
img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8))
al = Image.fromarray((alpha * 255).astype(np.uint8))
d = ImageDraw.Draw(img)
dm = ImageDraw.Draw(al)
for x0, x1_ in ((380, 905),):
    d.line([(x0, FLOOR_Y), (x1_, FLOOR_Y)], fill=(0x2E, 0xF0, 0xA0), width=2)
    dm.line([(x0, FLOOR_Y), (x1_, FLOOR_Y)], fill=130, width=2)
    d.line([(x0, FLOOR_Y - 8), (x0, FLOOR_Y)], fill=(0x2E, 0xF0, 0xA0), width=2)
    dm.line([(x0, FLOOR_Y - 8), (x0, FLOOR_Y)], fill=130, width=2)

# dim static halo for hosts that ignore alpha: only where the cow does not cover it
tick_layer = Image.new("RGB", (W, H), (0, 0, 0))
td = ImageDraw.Draw(tick_layer)
cols = [(0xA7, 0x7B, 0xFF), (0x2E, 0xF0, 0xA0), (0xFF, 0x4D, 0x5E), (0x69, 0x8E, 0xFF)]
N = 48
for i in range(N):
    ang = 2 * math.pi * i / N
    c = cols[i % 4]
    c = tuple(int(v * 0.32) for v in c)
    p0 = (HALO_CX + math.cos(ang) * HALO_R0, HALO_CY + math.sin(ang) * HALO_R0)
    p1 = (HALO_CX + math.cos(ang) * HALO_R1, HALO_CY + math.sin(ang) * HALO_R1)
    td.line([p0, p1], fill=c, width=3)
ticks = np.asarray(tick_layer.filter(ImageFilter.GaussianBlur(0.6))).astype(np.float32)
base = np.asarray(img).astype(np.float32)
under = np.asarray(al).astype(np.float32) / 255.0
base = np.clip(base + ticks * (1 - under[..., None]), 0, 255)
out = np.dstack([base, np.asarray(al).astype(np.float32)]).astype(np.uint8)
Image.fromarray(out, "RGBA").save(HERE / "art" / "faceplate-bg-1280x630.png", optimize=True)
print("wrote", HERE / "art" / "faceplate-bg-1280x630.png", "cow coverage %.1f%%" % (100 * cow.mean()))
