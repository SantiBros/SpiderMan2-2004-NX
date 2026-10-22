#!/usr/bin/env python3
"""make_art.py <fonts dir> <banner picture> <social picture> <out dir>: the README's art, in the style of the
NFS Most Wanted 2012 port's.

  banner.jpg            2560x800, the title over a frame of the game fading into the dark
  social-preview.jpg    1280x640, for GitHub's social preview (Settings, General, Social preview)
  install-steps.png     2400x760, the four install steps (and install-steps.es.png, in Spanish)

The pictures are frames from the port's PC test build (cap/gl_*.tga). The font is Barlow Condensed (SIL Open Font
License 1.1), not included: the fonts dir holds BarlowCondensed-<weight>-<normal|italic>.ttf, e.g. from the
@fontsource/barlow-condensed package converted with fontTools."""
import os
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

BG = (10, 13, 24)          # night navy, near black
PANEL = (20, 25, 40)
LINE = (52, 60, 84)
RED = (220, 30, 46)
WHITE = (255, 255, 255)
LIGHT = (222, 226, 236)
GRAY = (150, 158, 178)

FONTS = ""


def font(weight, italic, size):
    return ImageFont.truetype(os.path.join(FONTS, f"BarlowCondensed-{weight}-{'italic' if italic else 'normal'}.ttf"), size)


def spaced(d, xy, text, f, fill, spacing):
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + spacing
    return x


def frame_picture(path, w, h, crop_top=0.0, crop_bottom=0.0, crop_left=0.0, crop_right=0.0):
    """the game frame cut to its region of interest and scaled to cover w x h"""
    im = Image.open(path).convert("RGB")
    W, H = im.size
    im = im.crop((int(W * crop_left), int(H * crop_top), int(W * (1 - crop_right)), int(H * (1 - crop_bottom))))
    s = max(w / im.width, h / im.height)
    im = im.resize((int(im.width * s + 0.5), int(im.height * s + 0.5)), Image.LANCZOS)
    x, y = (im.width - w) // 2, (im.height - h) // 2
    im = im.crop((x, y, x + w, y + h))
    im = im.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
    return ImageEnhance.Contrast(ImageEnhance.Color(im).enhance(1.08)).enhance(1.05)


def fade_mask(w, h, start, end, power=1.6):
    """0 left of start, 255 right of end, eased between"""
    m = Image.new("L", (w, h))
    row = []
    for x in range(w):
        t = min(max((x - start) / max(end - start, 1), 0.0), 1.0)
        row.append(int(255 * (t ** power)))
    line = Image.new("L", (w, 1))
    line.putdata(row)
    return line.resize((w, h))


def vignette(img, strength=0.55):
    """darker toward the top and bottom edges"""
    w, h = img.size
    m = Image.new("L", (1, h))
    m.putdata([int(255 * strength * (abs(y / (h - 1) - 0.5) * 2) ** 2.2) for y in range(h)])
    shade = Image.new("RGB", (w, h), BG)
    img.paste(shade, (0, 0), m.resize((w, h)))


def badge(d, x, y, text, f, filled, pad_x=26, h=70):
    w = int(d.textlength(text, font=f)) + 2 * pad_x
    if filled:
        d.rectangle([x, y, x + w, y + h], fill=RED)
    else:
        d.rectangle([x, y, x + w, y + h], outline=(120, 128, 150), width=2)
    bbox = d.textbbox((0, 0), text, font=f)
    d.text((x + pad_x, y + (h - (bbox[3] - bbox[1])) // 2 - bbox[1]), text, font=f, fill=WHITE)
    return x + w


def red_rule(img, y, x0, x1, thick, fade_from):
    """a red line that fades out toward x1"""
    d = ImageDraw.Draw(img)
    for x in range(x0, x1):
        t = 1.0 if x < fade_from else max(0.0, 1 - (x - fade_from) / max(x1 - fade_from, 1))
        c = tuple(int(BG[i] + (RED[i] - BG[i]) * t) for i in range(3))
        d.line([(x, y), (x, y + thick - 1)], fill=c)


def banner(picture, out):
    W, H = 2560, 800
    img = Image.new("RGB", (W, H), BG)
    pic_w = 1560
    pic = frame_picture(picture, pic_w, H, **PICTURE_CROP)
    layer = Image.new("RGB", (W, H), BG)
    layer.paste(pic, (W - pic_w, 0))
    img.paste(layer, (0, 0), fade_mask(W, H, W - pic_w - 40, W - pic_w + 640))
    vignette(img, 0.6)
    d = ImageDraw.Draw(img)
    x0 = 140
    spaced(d, (x0, 96), "UNOFFICIAL HOMEBREW PORT  ·  ORIGINAL XBOX VERSION", font(700, False, 46), RED, 9)
    d.text((x0 - 6, 150), "SPIDER-MAN 2", font=font(900, True, 268), fill=WHITE)
    x = x0
    big = font(900, True, 196)
    d.text((x, 404), "XBOX", font=big, fill=RED)
    x += int(d.textlength("XBOX", font=big)) + 34
    d.text((x, 490), "FOR NINTENDO SWITCH", font=font(700, True, 98), fill=LIGHT)
    bf = font(700, False, 40)
    by = 642
    x = badge(d, x0, by, "STATIC RECOMPILATION", bf, True) + 20
    x = badge(d, x, by, "5 LANGUAGES", bf, False) + 20
    badge(d, x, by, "BRING YOUR OWN COPY", bf, False)
    red_rule(img, by + 34, 0, x0 - 24, 3, 0)
    red_rule(img, by + 100, 0, x0 + 380, 2, 60)
    red_rule(img, H - 10, 0, W, 10, int(W * 0.55))
    img.save(os.path.join(out, "banner.jpg"), quality=92)


def social(picture, out):
    W, H = 1280, 640
    img = Image.new("RGB", (W, H), BG)
    pic_w = 860
    pic = frame_picture(picture, pic_w, H, **PICTURE_CROP)
    layer = Image.new("RGB", (W, H), BG)
    layer.paste(pic, (W - pic_w, 0))
    img.paste(layer, (0, 0), fade_mask(W, H, W - pic_w - 20, W - pic_w + 420))
    vignette(img, 0.6)
    d = ImageDraw.Draw(img)
    x0 = 72
    spaced(d, (x0, 92), "UNOFFICIAL HOMEBREW PORT", font(700, False, 30), RED, 6)
    d.text((x0 - 4, 128), "SPIDER-MAN 2", font=font(900, True, 150), fill=WHITE)
    big = font(900, True, 112)
    d.text((x0, 274), "XBOX", font=big, fill=RED)
    x = x0 + int(d.textlength("XBOX", font=big)) + 20
    d.text((x, 322), "FOR NINTENDO SWITCH", font=font(700, True, 56), fill=LIGHT)
    bf = font(700, False, 27)
    x = badge(d, x0, 446, "STATIC RECOMPILATION", bf, True, 18, 50) + 14
    badge(d, x, 446, "BRING YOUR OWN COPY", bf, False, 18, 50)
    d.text((x0, 532), "github.com/SantiBros/SpiderMan2-2004-NX", font=font(500, False, 30), fill=GRAY)
    red_rule(img, H - 8, 0, W, 8, int(W * 0.5))
    img.save(os.path.join(out, "social-preview.jpg"), quality=92)


# the four install steps' icons, drawn as lines in a box of s pixels at (x, y)
def icon_download(d, x, y, s, c, w):
    cx = x + s / 2
    d.line([(cx, y + s * 0.08), (cx, y + s * 0.62)], fill=c, width=w)
    d.line([(cx - s * 0.24, y + s * 0.40), (cx, y + s * 0.64), (cx + s * 0.24, y + s * 0.40)], fill=c, width=w, joint="curve")
    d.line([(x + s * 0.12, y + s * 0.70), (x + s * 0.12, y + s * 0.88), (x + s * 0.88, y + s * 0.88), (x + s * 0.88, y + s * 0.70)], fill=c, width=w, joint="curve")


def icon_build(d, x, y, s, c, w):
    # a terminal window with a prompt
    d.rounded_rectangle([x + s * 0.04, y + s * 0.12, x + s * 0.96, y + s * 0.88], radius=s * 0.08, outline=c, width=w)
    d.line([(x + s * 0.04, y + s * 0.30), (x + s * 0.96, y + s * 0.30)], fill=c, width=w)
    d.line([(x + s * 0.20, y + s * 0.46), (x + s * 0.36, y + s * 0.59), (x + s * 0.20, y + s * 0.72)], fill=c, width=w, joint="curve")
    d.line([(x + s * 0.46, y + s * 0.73), (x + s * 0.74, y + s * 0.73)], fill=c, width=w)


def icon_sd(d, x, y, s, c, w):
    pts = [(x + s * 0.24, y + s * 0.06), (x + s * 0.64, y + s * 0.06), (x + s * 0.80, y + s * 0.22),
           (x + s * 0.80, y + s * 0.94), (x + s * 0.24, y + s * 0.94), (x + s * 0.24, y + s * 0.06)]
    d.line(pts, fill=c, width=w, joint="curve")
    for i in range(3):
        xx = x + s * (0.36 + i * 0.11)
        d.line([(xx, y + s * 0.18), (xx, y + s * 0.34)], fill=c, width=w)


def icon_play(d, x, y, s, c, w):
    d.rounded_rectangle([x + s * 0.04, y + s * 0.16, x + s * 0.96, y + s * 0.84], radius=s * 0.16, outline=c, width=w)
    d.polygon([(x + s * 0.42, y + s * 0.34), (x + s * 0.42, y + s * 0.66), (x + s * 0.66, y + s * 0.50)], outline=c, width=w)


STEPS = {
    "en": [("DOWNLOAD", [("the ", 0), ("SpiderMan2-2004-NX", 1), (" zip from ", 0), ("Releases", 1)], icon_download),
           ("BUILD", [("double-click ", 0), ("build-windows.cmd", 1), (" and choose your game", 0)], icon_build),
           ("COPY TO SD", [("the ", 0), ("switch", 1), (" folder it made, with your game in it, to the SD card", 0)], icon_sd),
           ("PLAY", [("hold ", 0), ("R", 1), (" while starting any game, then open ", 0), ("Spider-Man 2 (Xbox)", 1)], icon_play)],
    "es": [("DESCARGA", [("el zip ", 0), ("SpiderMan2-2004-NX", 1), (" de ", 0), ("Releases", 1)], icon_download),
           ("COMPILA", [("doble clic en ", 0), ("build-windows.cmd", 1), (" y elige tu juego", 0)], icon_build),
           ("COPIA A LA SD", [("la carpeta ", 0), ("switch", 1), (" que creó, con tu juego dentro, a la tarjeta SD", 0)], icon_sd),
           ("JUEGA", [("mantén ", 0), ("R", 1), (" al abrir un juego y elige ", 0), ("Spider-Man 2 (Xbox)", 1)], icon_play)],
}
FOOTER = {
    "en": "Your own copy of the original Xbox game, and a PC to build it once (on Linux, with build.sh).",
    "es": "Tu propia copia del juego de la Xbox original, y un PC para compilarlo una vez (en Linux, con build.sh).",
}


def wrap_runs(d, runs, fonts, width):
    """runs of (text, bold) into lines of (text, bold) pieces no wider than width, breaking at spaces"""
    words = []
    for text, bold in runs:
        parts = text.split(" ")
        for i, p in enumerate(parts):
            if p:
                words.append([p, bold, False])
            if i < len(parts) - 1:
                if words:
                    words[-1][2] = True  # a space follows
    lines, cur, cur_w = [], [], 0
    for word, bold, space in words:
        f = fonts[bold]
        ww = d.textlength(word, font=f)
        sw = d.textlength(" ", font=f) if space else 0
        if cur and cur_w + ww > width:
            lines.append(cur)
            cur, cur_w = [], 0
        cur.append((word + (" " if space else ""), bold))
        cur_w += ww + sw
    if cur:
        lines.append(cur)
    return lines


def steps(lang, out):
    W, H = 2400, 760
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    # a soft light from the top left
    glow = Image.new("RGB", (W, H), (26, 32, 52))
    img.paste(glow, (0, 0), fade_mask(W, H, -W, int(W * 0.9), 1.0).transpose(Image.FLIP_LEFT_RIGHT))
    d = ImageDraw.Draw(img)
    n, margin, gap = 4, 80, 66
    cw = (W - 2 * margin - (n - 1) * gap) // n
    ch, top = 476, 68
    title_f = font(800, True, 66)
    body = {0: font(500, False, 38), 1: font(700, False, 38)}
    for i, (title, runs, icon) in enumerate(STEPS[lang]):
        x = margin + i * (cw + gap)
        d.rounded_rectangle([x, top, x + cw, top + ch], radius=22, fill=PANEL, outline=LINE, width=2)
        # the number tab
        tw, th = 92, 80
        d.rounded_rectangle([x + cw - tw, top, x + cw, top + th], radius=22, fill=RED)
        d.rectangle([x + cw - tw, top + th - 22, x + cw, top + th], fill=RED)
        d.rectangle([x + cw - tw, top, x + cw - tw + 22, top + th], fill=RED)
        nf = font(800, True, 58)
        num = str(i + 1)
        nb = d.textbbox((0, 0), num, font=nf)
        d.text((x + cw - tw / 2 - (nb[2] - nb[0]) / 2 - nb[0], top + th / 2 - (nb[3] - nb[1]) / 2 - nb[1]), num, font=nf, fill=WHITE)
        icon(d, x + 44, top + 54, 96, LIGHT, 7)
        d.text((x + 44, top + 196), title, font=title_f, fill=WHITE)
        y = top + 290
        for line in wrap_runs(d, runs, body, cw - 88):
            xx = x + 44
            for text, bold in line:
                d.text((xx, y), text, font=body[bold], fill=WHITE if bold else GRAY)
                xx += d.textlength(text, font=body[bold])
            y += 48
        if i < n - 1:
            ax, ay = x + cw + gap / 2, top + ch / 2
            d.polygon([(ax - 12, ay - 22), (ax + 12, ay), (ax - 12, ay + 22), (ax - 4, ay)], fill=RED)
    fy = top + ch + 70
    d.rectangle([margin, fy + 22, margin + 46, fy + 28], fill=RED)
    d.text((margin + 70, fy), FOOTER[lang], font=font(500, False, 42), fill=LIGHT)
    red_rule(img, H - 8, 0, W, 8, int(W * 0.5))
    img.save(os.path.join(out, "install-steps.png" if lang == "en" else f"install-steps.{lang}.png"), optimize=True)


PICTURE_CROP = {}

if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    FONTS, banner_pic, social_pic, out = sys.argv[1:5]
    # the frame's top three quarters: the game's compass and tutorial boxes sit at the bottom
    PICTURE_CROP = {"crop_bottom": float(os.environ.get("CROP_BOTTOM", "0.22")), "crop_top": float(os.environ.get("CROP_TOP", "0"))}
    os.makedirs(out, exist_ok=True)
    banner(banner_pic, out)
    social(social_pic, out)
    for lang in STEPS:
        steps(lang, out)
