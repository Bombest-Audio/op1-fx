"""Build art/faceplate-bg-1280x630.png: the Higgsfield glitch-line picture on the near-black plate.
Usage: python3 compose_faceplate.py   (reads art/line-1.png, writes art/faceplate-bg-1280x630.png)"""
from PIL import Image, ImageChops, ImageFilter
import os

here = os.path.dirname(os.path.abspath(__file__))
PLATE = (7, 8, 12)
W, H = 1280, 630
ART = 700          # the square picture is drawn at 700 x 700
AX, AY = 305, -25  # top-left of the picture on the plate

src = Image.open(os.path.join(here, "art", "line-1.png")).convert("RGB").resize((ART, ART), Image.LANCZOS)
plate = Image.new("RGB", (W, H), PLATE)
# Lighten blend so the picture's own near-black background never darkens the plate.
floor = Image.new("RGB", src.size, PLATE)
lit = ImageChops.lighter(src, floor)
# Feather mask: fade the picture out toward its edges and the bottom.
mask = Image.new("L", src.size, 0)
px = mask.load()
for y in range(ART):
    for x in range(ART):
        ex = min(x, ART - 1 - x) / (ART * 0.22)
        ey_top = y / (ART * 0.22)
        ey_bot = (ART - 1 - y) / (ART * 0.30)
        v = max(0.0, min(1.0, ex, ey_top, ey_bot))
        px[x, y] = int(255 * v * v * (3 - 2 * v))
plate.paste(lit, (AX, AY), mask)
plate.save(os.path.join(here, "art", "faceplate-bg-1280x630.png"), optimize=True)
print("wrote faceplate-bg-1280x630.png")
