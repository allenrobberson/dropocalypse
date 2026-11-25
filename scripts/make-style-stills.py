#!/usr/bin/env python3
"""Saves one still from each style's preview GIF for the PDF guide (a PDF can't play GIFs).

Reads docs/media/styles/*.gif (not the logo layers demos) and writes
docs/guide-images/styles/<name>.jpg, picking a busy frame from the middle of the clip.
"""
import glob
import os

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out_dir = os.path.join(ROOT, "docs", "guide-images", "styles")
    os.makedirs(out_dir, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(ROOT, "docs", "media", "styles", "*.gif"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name.startswith("layers-") or name == "logo-layers":
            continue
        im = Image.open(path)
        n = im.n_frames
        best, best_score = None, -1.0
        for f in range(n // 4, 3 * n // 4, 2):  # the middle half: past any fade-in
            im.seek(f)
            fr = im.convert("RGB")
            score = float(np.asarray(fr, dtype=np.float32).std())
            if score > best_score:
                best, best_score = fr.copy(), score
        best.save(os.path.join(out_dir, name + ".jpg"), quality=85)
    print("wrote stills to", out_dir)


if __name__ == "__main__":
    main()
