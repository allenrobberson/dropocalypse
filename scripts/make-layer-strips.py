#!/usr/bin/env python3
"""Turns the logo layers preview GIFs into still filmstrips for the PDF guide.

Reads docs/media/styles/layers-<move>.gif and writes docs/guide-images/layers-<move>.jpg: four
moments side by side, cropped to the logo, so the printed guide shows how each move looks.
"""
import os

from PIL import Image

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOVES = ["wave", "explode", "bands", "scatter"]


def main():
    for move in MOVES:
        src = os.path.join(ROOT, "docs", "media", "styles", f"layers-{move}.gif")
        im = Image.open(src)
        frames = []
        for f in range(im.n_frames):
            im.seek(f)
            frames.append(im.convert("RGB"))
        # The 4 frames where the letters are furthest from rest, spread through the clip.
        def spread(fr):
            a = np.asarray(fr.convert("L"))[:150]
            ys, xs = np.where(a > 235)
            return (xs.max() - xs.min()) + 2 * (ys.max() - ys.min()) if len(xs) else 0
        n = len(frames)
        picks = []
        for q in range(4):
            seg = list(range(q * n // 4, (q + 1) * n // 4))
            picks.append(max(seg, key=lambda i: spread(frames[i])))
        crop = (0, 10, 360, 150)
        w, h = crop[2] - crop[0], crop[3] - crop[1]
        strip = Image.new("RGB", (w * 4 + 6 * 3, h), (24, 20, 32))
        for k, i in enumerate(picks):
            strip.paste(frames[i].crop(crop), (k * (w + 6), 0))
        out = os.path.join(ROOT, "docs", "guide-images", f"layers-{move}.jpg")
        strip.save(out, quality=88)
        print("wrote", out)


if __name__ == "__main__":
    main()
