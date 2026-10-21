#!/usr/bin/env python3
"""contact.py out.png cols frame1.tga frame2.tga ... : a labelled contact sheet, half size."""
import sys
from PIL import Image, ImageDraw
out, cols, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
ims = [Image.open(f).convert("RGB") for f in files]
w, h = ims[0].size[0] // 2, ims[0].size[1] // 2
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * w, rows * (h + 14)), (40, 40, 40))
d = ImageDraw.Draw(sheet)
for i, (im, f) in enumerate(zip(ims, files)):
    x, y = (i % cols) * w, (i // cols) * (h + 14)
    sheet.paste(im.resize((w, h)), (x, y + 14))
    d.text((x + 3, y + 1), f.split("/")[-1].replace(".tga", ""), fill=(255, 255, 0))
sheet.save(out)
print(sheet.size)
