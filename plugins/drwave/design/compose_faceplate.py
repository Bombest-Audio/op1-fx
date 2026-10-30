"""Build art/faceplate-bg-1280x630.png: the Higgsfield head-mirror picture on the near-black plate.
Usage: python3 compose_faceplate.py   (reads art/mirror-a.png, writes art/faceplate-bg-1280x630.png)"""
from PIL import Image, ImageChops
import os

here = os.path.dirname(os.path.abspath(__file__))
PLATE = (7, 8, 12)
W, H = 1280, 630
ART = 540          # the square picture is drawn at 540 x 540
AX, AY = 370, 20   # top-left of the picture on the plate

src = Image.open(os.path.join(here, "art", "mirror-a.png")).convert("RGB").resize((ART, ART), Image.LANCZOS)
plate = Image.new("RGB", (W, H), PLATE)
lit = ImageChops.lighter(src, Image.new("RGB", src.size, PLATE))
mask = Image.new("L", src.size, 0)
px = mask.load()
for y in range(ART):
    for x in range(ART):
        e = min(min(x, ART - 1 - x) / (ART * 0.18), y / (ART * 0.18), (ART - 1 - y) / (ART * 0.18))
        v = max(0.0, min(1.0, e))
        px[x, y] = int(255 * v * v * (3 - 2 * v))
plate.paste(lit, (AX, AY), mask)
plate.save(os.path.join(here, "art", "faceplate-bg-1280x630.png"), optimize=True)
print("wrote faceplate-bg-1280x630.png")
