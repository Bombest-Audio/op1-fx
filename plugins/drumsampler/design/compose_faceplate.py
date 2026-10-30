"""Compose faceplate-bg-1280x630.png: the generated record (art/record-1.png) on the plate, dimmed, centered at (640, 330)."""
from PIL import Image, ImageChops, ImageEnhance
rec = Image.open("art/record-1.png").convert("RGB").crop((100, 100, 924, 924))   # tight around the disc
rec = rec.resize((620, 620), Image.LANCZOS)
rec = ImageEnhance.Brightness(rec).enhance(0.75)
plate = Image.new("RGB", (1280, 630), (7, 8, 12))
layer = Image.new("RGB", (1280, 630), (0, 0, 0))
layer.paste(rec, (640 - 310, 330 - 310))
out = ImageChops.lighter(plate, layer)
out.save("art/faceplate-bg-1280x630.png")
