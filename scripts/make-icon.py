#!/usr/bin/env python3
"""Draws the Dropocalypse icon (original artwork): rainbow equalizer bars inside a neon teardrop,
on a transparent background, and writes every platform's icon files:

  Resources/AppIcon-1024.png, Resources/AppIcon.icns     Mac
  windows/dropocalypse.ico                               Windows (portable .exe)
  windows/store/Assets/*.png                             Microsoft Store (MSIX tiles and logos)
  linux/dropocalypse.png, linux/src/icon_png.h           Linux (desktop file, window icon)
  docs/media/icon-mark.png                               README / website

Run it after changing the artwork: python3 scripts/make-icon.py
"""
import colorsys, io, math, os
from PIL import Image, ImageChops, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 4096                      # draw large, downsample for smooth edges
OUTLINE = (120, 240, 255)     # neon cyan


def drop_polygon(cx, cy, w, h, steps=6000, m=1.3):
    """Classic teardrop (tip at the top), scaled to fit w x h centred on (cx, cy)."""
    raw = [(math.sin(t) * math.sin(t / 2) ** m, -math.cos(t))
           for t in (2 * math.pi * i / steps for i in range(steps))]
    xs = [p[0] for p in raw]
    half = max(abs(min(xs)), max(xs))
    return [(cx + x / half * w / 2, cy + y * h / 2) for x, y in raw]


def draw_mark(stroke=0.008, n=9):
    """The mark on a transparent S x S canvas, cropped to its visible shape.
    `stroke` is half the outline width as a fraction of S, and `n` the number of bars: tiny icons
    use fewer, thicker bars and a bolder outline so they stay crisp at 16-32 px."""
    bars = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bars)
    left, right = S * 0.27, S * 0.73
    bw = (right - left) / n
    # A symmetric mountain of bars, tallest in the middle (9 bars: 0.30, 0.48 ... 1.0 ... 0.30).
    heights = [0.30 + 0.70 * (1 - abs(i - (n - 1) / 2) / ((n - 1) / 2)) for i in range(n)]
    gap = 0.14 if n > 5 else 0.2
    base = S * 0.80
    for i, hgt in enumerate(heights):
        hue = 0.83 - i / (n - 1) * 0.72          # magenta -> orange
        r, g, b = colorsys.hsv_to_rgb(hue % 1, 0.85, 1.0)
        bd.rounded_rectangle([left + i * bw + bw * gap, base - hgt * S * 0.55,
                              left + (i + 1) * bw - bw * gap, base],
                             radius=bw * 0.3, fill=(int(r * 255), int(g * 255), int(b * 255), 255))
    pts = drop_polygon(S * 0.5, S * 0.53, S * 0.56, S * 0.70)
    drop = Image.new("L", (S, S), 0)
    ImageDraw.Draw(drop).polygon(pts, fill=255)
    bars.putalpha(ImageChops.multiply(bars.getchannel("A"), drop))

    # Outline: a chain of discs along the edge gives an even stroke with a rounded tip.
    half = S * stroke
    ring = Image.new("L", (S, S), 0)
    rd = ImageDraw.Draw(ring)
    for x, y in pts:
        rd.ellipse([x - half, y - half, x + half, y + half], fill=255)
    line = Image.new("RGBA", (S, S), OUTLINE + (0,))
    line.putalpha(ring)
    mark = Image.alpha_composite(bars, line)
    return mark.crop(mark.getchannel("A").getbbox())


def place(mark, size, fill, width=None):
    """The mark centred on a transparent size x size (or width x size) canvas, its longer side
    taking `fill` of the shorter canvas side."""
    width = width or size
    scale = fill * min(width, size) / max(mark.width, mark.height)
    m = mark.resize((max(1, round(mark.width * scale)), max(1, round(mark.height * scale))), Image.LANCZOS)
    out = Image.new("RGBA", (width, size), (0, 0, 0, 0))
    out.paste(m, ((width - m.width) // 2, (size - m.height) // 2), m)
    return out


def main():
    big = draw_mark()
    medium = draw_mark(stroke=0.016)        # 33-48 px: thicker outline
    tiny = draw_mark(stroke=0.03, n=5)      # 32 px and below: 5 bars, bold outline

    def icon(size, fill):
        return place(tiny if size <= 32 else medium if size <= 48 else big, size, fill)

    # Mac: the artwork fills about 80% of the 1024 canvas, like other Mac app icons.
    res = os.path.join(ROOT, "Resources")
    os.makedirs(res, exist_ok=True)
    mac = icon(1024, 0.80)
    mac.save(os.path.join(res, "AppIcon-1024.png"))
    mac.save(os.path.join(res, "AppIcon.icns"),
             sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])

    # Windows .ico: each size drawn separately so the small ones get the thicker outline.
    ico_sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [icon(s, 0.92) for s in ico_sizes]
    frames[-1].save(os.path.join(ROOT, "windows", "dropocalypse.ico"), sizes=[(s, s) for s in ico_sizes],
                    append_images=frames[:-1])

    # Microsoft Store tiles and logos (the tile background is transparent in AppxManifest.xml).
    assets = os.path.join(ROOT, "windows", "store", "Assets")
    os.makedirs(assets, exist_ok=True)
    square = {"Square44x44Logo": 44, "Square71x71Logo": 71, "Square150x150Logo": 150,
              "Square310x310Logo": 310, "StoreLogo": 50}
    for name, s in square.items():
        fill = 0.92 if name in ("Square44x44Logo", "StoreLogo") else 0.66
        icon(s, fill).save(os.path.join(assets, name + ".png"))
        icon(s * 2, fill).save(os.path.join(assets, name + ".scale-200.png"))
    for s in (16, 24, 32, 48, 256):
        icon(s, 0.92).save(os.path.join(assets, f"Square44x44Logo.targetsize-{s}_altform-unplated.png"))
    for scale, h in ((1, 150), (2, 300)):
        wide = place(big, h, 0.66, width=h * 310 // 150)
        wide.save(os.path.join(assets, "Wide310x150Logo" + (".scale-200" if scale == 2 else "") + ".png"))

    # Linux: desktop icon and the window icon compiled into the app.
    icon(256, 0.92).save(os.path.join(ROOT, "linux", "dropocalypse.png"))
    buf = io.BytesIO()
    icon(128, 0.92).save(buf, format="PNG", optimize=True)
    data = buf.getvalue()
    lines = [",".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    with open(os.path.join(ROOT, "linux", "src", "icon_png.h"), "w") as f:
        f.write("// Dropocalypse for Linux - window icon (128x128 PNG, generated by scripts/make-icon.py).\n")
        f.write("#pragma once\n\nstatic const unsigned char kIconPng[] = {\n")
        f.write(",\n".join("    " + l for l in lines) + "\n};\n")

    # README / website: the mark alone, tightly framed.
    place(big, 1024, 0.94).save(os.path.join(ROOT, "docs", "media", "icon-mark.png"))
    print("icons written")


if __name__ == "__main__":
    main()
