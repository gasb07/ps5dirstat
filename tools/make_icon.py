#!/usr/bin/env python3
"""Draws assets/icon0.png (512x512) for the PS5 DirStat Media tab shortcut."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

S = 512
img = Image.new('RGB', (S, S), (14, 17, 22))
d = ImageDraw.Draw(img)
# a small treemap
cells = [((40, 40, 300, 330), (61, 123, 253)), ((300, 40, 472, 200), (230, 140, 50)),
         ((300, 200, 472, 330), (70, 170, 110)), ((40, 330, 200, 400), (150, 90, 210)),
         ((200, 330, 360, 400), (200, 70, 80)), ((360, 330, 472, 400), (60, 170, 180))]
for box, color in cells:
    d.rectangle(box, fill=color, outline=(14, 17, 22), width=6)
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 64)
text = 'DirStat'
w = d.textlength(text, font=font)
d.text(((S - w) / 2, 412), text, font=font, fill=(232, 234, 237))
out = Path(__file__).resolve().parent.parent / 'assets' / 'icon0.png'
out.parent.mkdir(exist_ok=True)
img.save(out, optimize=True)
