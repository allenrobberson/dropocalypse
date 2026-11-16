#!/usr/bin/env python3
"""Draws the Dropocalypse app icon (original artwork) and writes
Resources/AppIcon.icns (Mac), Resources/AppIcon-1024.png and windows/dropocalypse.ico."""
import colorsys, math, os
from PIL import Image, ImageDraw, ImageFilter, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 2048                      # draw at 2x, downsample for smooth edges

def squircle_mask(size, inset, n=5.0):
    m = Image.new("L", (size, size), 0)
    px = m.load()
    c = size / 2; r = size / 2 - inset
    for y in range(size):
        dy = abs((y + 0.5 - c) / r)
        if dy > 1: continue
        dx = (1 - dy ** n) ** (1 / n) * r
        x0, x1 = int(c - dx), int(c + dx)
        for x in range(max(0, x0), min(size, x1 + 1)):
            px[x, y] = 255
    return m

def drop_polygon(cx, cy, w, h, steps=720, m=1.3):
    """Classic teardrop (tip at the top), scaled to fit w x h centred on (cx, cy)."""
    raw = [(math.sin(t) * math.sin(t / 2) ** m, -math.cos(t))
           for t in (2 * math.pi * i / steps for i in range(steps))]
    xs = [p[0] for p in raw]
    half = max(abs(min(xs)), max(xs))
    return [(cx + x / half * w / 2, cy + y * h / 2) for x, y in raw]

def main():
    # Background: deep violet-to-black radial gradient.
    bg = Image.new("RGB", (S, S))
    px = bg.load()
    for y in range(S):
        for x in range(S):
            d = math.hypot(x - S * 0.5, y - S * 0.42) / (S * 0.75)
            d = min(1, d)
            a = (1 - d) ** 1.6
            px[x, y] = (int(10 + 60 * a), int(4 + 10 * a), int(22 + 90 * a))
    img = bg.convert("RGBA")

    # Rainbow equalizer bars, clipped to a drop.
    bars = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bars)
    n = 9
    left, right = S * 0.27, S * 0.73
    bw = (right - left) / n
    heights = [0.30, 0.48, 0.66, 0.84, 1.0, 0.84, 0.66, 0.48, 0.30]
    base = S * 0.80
    for i, hgt in enumerate(heights):
        hue = 0.83 - i / (n - 1) * 0.72          # magenta -> orange
        r, g, b = colorsys.hsv_to_rgb(hue % 1, 0.85, 1.0)
        x0 = left + i * bw + bw * 0.14
        x1 = left + (i + 1) * bw - bw * 0.14
        y0 = base - hgt * S * 0.55
        bd.rounded_rectangle([x0, y0, x1, base], radius=bw * 0.3,
                             fill=(int(r * 255), int(g * 255), int(b * 255), 255))
    drop = Image.new("L", (S, S), 0)
    ImageDraw.Draw(drop).polygon(drop_polygon(S * 0.5, S * 0.53, S * 0.56, S * 0.70), fill=255)
    bars.putalpha(ImageChops.multiply(bars.getchannel("A"), drop))

    # Glow behind the bars.
    glow = bars.filter(ImageFilter.GaussianBlur(S * 0.03))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, bars)

    # Neon drop outline: a smooth ring taken from a blurred copy of the drop mask.
    soft = drop.filter(ImageFilter.GaussianBlur(S * 0.012))
    ring = soft.point(lambda v: int(max(0, 1 - abs(v - 128) / 70) ** 0.6 * 255) if 40 < v < 216 else 0)
    line = Image.new("RGBA", (S, S), (120, 240, 255, 0)); line.putalpha(ring)
    img = Image.alpha_composite(img, line.filter(ImageFilter.GaussianBlur(S * 0.014)))
    img = Image.alpha_composite(img, line)

    # Shape it as a macOS app icon: squircle with margin (824/1024 body).
    mask = squircle_mask(S, int(S * (100 / 1024)))
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    icon = out.resize((1024, 1024), Image.LANCZOS)

    res = os.path.join(ROOT, "Resources"); os.makedirs(res, exist_ok=True)
    icon.save(os.path.join(res, "AppIcon-1024.png"))
    icon.save(os.path.join(res, "AppIcon.icns"),
              sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
    # Windows: full-bleed square crop of the artwork with rounded corners.
    win = img.resize((256, 256), Image.LANCZOS)
    wm = Image.new("L", (256, 256), 0); ImageDraw.Draw(wm).rounded_rectangle([0, 0, 255, 255], 48, fill=255)
    win.putalpha(wm)
    win.save(os.path.join(ROOT, "windows", "dropocalypse.ico"),
             sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("icon written")

if __name__ == "__main__":
    main()
