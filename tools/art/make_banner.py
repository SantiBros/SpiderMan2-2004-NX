#!/usr/bin/env python3
"""make_banner.py <out dir>: the README banner (1600x500) and the social preview (1280x640), drawn from scratch:
a night skyline of plain blocks with lit windows, a moon, and the title in DejaVu Sans. No game art, no logos."""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

NAVY_TOP = (12, 18, 38)
NAVY_LOW = (36, 30, 70)
RED = (200, 32, 47)
AMBER = (255, 200, 64)
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf"


def sky(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(NAVY_TOP, NAVY_LOW)))
    rng = random.Random(4)
    for _ in range(w * h // 2500):
        x, y = rng.randrange(w), rng.randrange(int(h * 0.7))
        v = rng.randrange(120, 230)
        d.point((x, y), fill=(v, v, v + 20 if v < 235 else 255))
    return img


def moon(img, cx, cy, r):
    # a soft glow, blurred on its own, then the moon sharp on top
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([cx - r * 2.2, cy - r * 2.2, cx + r * 2.2, cy + r * 2.2], fill=(255, 225, 160, 46))
    glow = glow.filter(ImageFilter.GaussianBlur(r * 0.9))
    img.paste(glow, (0, 0), glow)
    disc = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(disc).ellipse([cx - r, cy - r, cx + r, cy + r], fill=(250, 240, 215, 255))
    disc = disc.filter(ImageFilter.GaussianBlur(1.0))
    img.paste(disc, (0, 0), disc)


def skyline(img, base, seed, scale, color, lit):
    w, h = img.size
    d = ImageDraw.Draw(img)
    rng = random.Random(seed)
    x = -20
    while x < w:
        bw = int(rng.randrange(40, 110) * scale)
        bh = int(rng.randrange(80, 300) * scale)
        top = base - bh
        d.rectangle([x, top, x + bw, h], fill=color)
        if rng.random() < 0.25:  # a stepped top or a mast
            sw = bw // 2
            d.rectangle([x + bw // 4, top - int(30 * scale), x + bw // 4 + sw, top], fill=color)
            if rng.random() < 0.5:
                d.rectangle([x + bw // 2 - 1, top - int(70 * scale), x + bw // 2 + 1, top - int(30 * scale)], fill=color)
        # windows
        for wy in range(top + 8, h - 4, int(14 * scale) + 4):
            for wx in range(x + 6, x + bw - 6, int(10 * scale) + 4):
                if rng.random() < lit:
                    c = rng.choice([AMBER, (255, 225, 140), (190, 210, 255)])
                    d.rectangle([wx, wy, wx + int(4 * scale), wy + int(6 * scale)], fill=c)
        x += bw + rng.randrange(0, 12)


def text(img, lines):
    d = ImageDraw.Draw(img)
    for (s, size, bold, fill, x, y, shadow) in lines:
        f = ImageFont.truetype(FONT_BOLD if bold else FONT, size)
        if shadow:
            d.text((x + 4, y + 4), s, font=f, fill=(0, 0, 0))
        d.text((x, y), s, font=f, fill=fill)


def badge(img, s, x, y, size, bg, fg):
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT_BOLD, size)
    l, t, r, b = d.textbbox((0, 0), s, font=f)
    pad = size // 2
    d.rounded_rectangle([x, y, x + (r - l) + 2 * pad, y + (b - t) + pad + 4], radius=size // 3, fill=bg)
    d.text((x + pad - l, y + pad // 2 + 2 - t), s, font=f, fill=fg)
    return x + (r - l) + 2 * pad


def scene(w, h, moon_at):
    img = sky(w, h)
    moon(img, *moon_at)
    skyline(img, int(h * 0.80), 11, 0.75, (24, 26, 52), 0.10)
    skyline(img, int(h * 0.92), 7, 1.0, (10, 12, 26), 0.16)
    # a dark band behind the text
    shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shade).rectangle([0, 0, int(w * 0.70), h], fill=(6, 8, 18, 120))
    shade = shade.filter(ImageFilter.GaussianBlur(60))
    img.paste(shade, (0, 0), shade)
    return img


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    img = scene(1600, 500, (1330, 120, 58))
    text(img, [
        ("SPIDER-MAN 2", 132, True, (255, 255, 255), 70, 70, True),
        ("ORIGINAL XBOX (2004)  ·  FOR NINTENDO SWITCH", 40, True, AMBER, 76, 230, True),
        ("An unofficial homebrew port, built from your own copy of the game", 30, False, (215, 220, 235), 76, 292, True),
    ])
    x = badge(img, "STATIC RECOMPILATION", 76, 370, 24, RED, (255, 255, 255))
    x = badge(img, "BRING YOUR OWN COPY", x + 16, 370, 24, (40, 52, 92), (255, 255, 255))
    badge(img, "EARLY TEST", x + 16, 370, 24, AMBER, (20, 20, 30))
    img.save(os.path.join(out, "banner.jpg"), quality=92)

    img = scene(1280, 640, (1060, 130, 52))
    text(img, [
        ("SPIDER-MAN 2", 124, True, (255, 255, 255), 64, 150, True),
        ("ORIGINAL XBOX (2004)", 44, True, AMBER, 70, 305, True),
        ("FOR NINTENDO SWITCH", 44, True, AMBER, 70, 360, True),
        ("Unofficial homebrew port  ·  bring your own copy", 30, False, (215, 220, 235), 70, 440, True),
        ("github.com/SantiBros/SpiderMan2-NX", 26, True, (170, 180, 210), 70, 560, False),
    ])
    img.save(os.path.join(out, "social-preview.jpg"), quality=92)


if __name__ == "__main__":
    main()
