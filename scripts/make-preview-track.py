#!/usr/bin/env python3
"""Writes a short synthetic dance loop (126 BPM) as a WAV file, for recording the README GIFs.

Kick on every beat, claps on 2 and 4, off-beat hats, a rolling bassline, chord stabs and a
riser every 8 bars, so every part of the spectrum moves. No recorded music is used.

    python3 scripts/make-preview-track.py track.wav [seconds]
"""
import sys
import wave

import numpy as np

SR = 44100
BPM = 126.0
BEAT = 60.0 / BPM


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "track.wav"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
    n = int(SR * seconds)
    t = np.arange(n) / SR
    mix = np.zeros(n)
    rng = np.random.default_rng(7)

    def add(start, sig, gain):
        i = int(start * SR)
        if i >= n:
            return
        j = min(n, i + len(sig))
        mix[i:j] += sig[: j - i] * gain

    # Kick: a falling sine with a click.
    kl = int(0.32 * SR)
    kt = np.arange(kl) / SR
    freq = 45 + 110 * np.exp(-kt * 28)
    kick = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-kt * 7.5)
    kick[:200] += np.linspace(0.6, 0, 200)
    # Clap / snare: filtered noise bursts.
    cl = int(0.22 * SR)
    ct = np.arange(cl) / SR
    clap = rng.standard_normal(cl) * np.exp(-ct * 18)
    clap = np.convolve(clap, [1, -0.6], mode="same")
    # Hat: short bright noise.
    hl = int(0.06 * SR)
    ht = np.arange(hl) / SR
    hat = np.diff(rng.standard_normal(hl + 1)) * np.exp(-ht * 60)

    beats = int(seconds / BEAT) + 1
    roots = [41.20, 41.20, 49.00, 36.71]  # E1, E1, G1, D1 (one per bar)
    for b in range(beats):
        start = b * BEAT
        bar = b // 4
        add(start, kick, 1.0)
        if b % 2 == 1:
            add(start, clap, 0.35)
        add(start + BEAT / 2, hat, 0.22)
        add(start + BEAT * 0.75, hat, 0.12)
        # Bass: two eighth notes per beat, off the kick.
        root = roots[bar % 4]
        for k, off in enumerate((0.5, 0.75)):
            bl = int(BEAT * 0.24 * SR)
            bt = np.arange(bl) / SR
            f = root * (2 if k else 1)
            saw = 2 * ((bt * f) % 1) - 1
            add(start + BEAT * off, np.tanh(saw * 2.5) * np.exp(-bt * 9), 0.32)
        # Chord stab on the off-beat of every other beat.
        if b % 2 == 0:
            sl = int(BEAT * 0.35 * SR)
            st = np.arange(sl) / SR
            stab = sum(np.sin(2 * np.pi * root * 8 * r * st) for r in (1.0, 1.26, 1.5, 2.0))
            add(start + BEAT * 0.5, stab * np.exp(-st * 10) / 4, 0.25)
        # Riser into every 8th bar.
        if b % 32 == 28:
            rl = int(BEAT * 4 * SR)
            rt = np.arange(rl) / SR
            sweep = np.sin(2 * np.pi * np.cumsum(300 + 2500 * (rt / rt[-1]) ** 2) / SR)
            noise = rng.standard_normal(rl) * 0.4
            add(start, (sweep + noise) * (rt / rt[-1]) ** 2, 0.18)

    mix = np.tanh(mix * 0.9)
    mix /= max(1e-6, np.max(np.abs(mix))) / 0.85
    pcm = (mix * 32767).astype("<i2")
    stereo = np.repeat(pcm[:, None], 2, axis=1).tobytes()
    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(stereo)
    print(f"wrote {out}: {seconds:.0f} s at {BPM:.0f} BPM")


if __name__ == "__main__":
    main()
