#!/usr/bin/env python3
"""Measures the plugin's frequency responses with the offline renderer and plots them.

Usage: python3 tools/plot_responses.py [path/to/ambience_render] [output_dir]
Requires numpy + matplotlib. Writes PNG figures (used in docs/DESIGN.md).
"""
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

RENDER = sys.argv[1] if len(sys.argv) > 1 else "build-tests/ambience_render"
OUT = sys.argv[2] if len(sys.argv) > 2 else "docs/img"
FS = 48000
N = 1 << 16

# Validated categorical palette (light surface), fixed order; direct labels provide relief
# for the lower-contrast slots.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#1f1f1e", "#6b6a64", "#e4e3dc"


def write_wav(path, x):
    data = np.asarray(x, dtype="<f4").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 3, 1, FS, FS * 4, 4, 32))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def read_wav(path):
    raw = open(path, "rb").read()
    ch = struct.unpack("<H", raw[22:24])[0]
    i = raw.index(b"data")
    n = struct.unpack("<I", raw[i + 4:i + 8])[0]
    return np.frombuffer(raw[i + 8:i + 8 + n], dtype="<f4").reshape(-1, ch).T


def response(settings, channels=1):
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "imp.wav"), os.path.join(d, "out.wav")
        x = np.zeros(N, dtype=np.float32)
        x[0] = 1.0
        write_wav(src, x)
        args = [RENDER, src, dst, "--channels", str(channels)]
        for k, v in settings.items():
            args += ["--set", f"{k}={v}"]
        subprocess.run(args, check=True, capture_output=True)
        y = read_wav(dst)
    f = np.fft.rfftfreq(N, 1 / FS)
    return f, [20 * np.log10(np.maximum(np.abs(np.fft.rfft(c)), 1e-12)) for c in y]


def style(ax, title, ylabel, ylim):
    ax.set_facecolor(SURFACE)
    ax.set_xscale("log")
    ax.set_xlim(20, 20000)
    ax.set_ylim(*ylim)
    ax.grid(True, which="major", color=GRID, linewidth=0.8)
    ax.grid(True, which="minor", color=GRID, linewidth=0.4, alpha=0.6)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.tick_params(colors=MUTED, labelsize=9)
    ax.set_xticks([31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000])
    ax.set_xticklabels(["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"])
    ax.set_xlabel("Frequency (Hz)", color=MUTED, fontsize=9)
    ax.set_ylabel(ylabel, color=MUTED, fontsize=9)
    ax.set_title(title, color=INK, fontsize=12, loc="left", fontweight="semibold")


def label_end(ax, f, y, text, colour, at=12000):
    i = np.searchsorted(f, at)
    ax.annotate(text, (f[i], y[i]), xytext=(4, 0), textcoords="offset points", color=INK, fontsize=9, va="center")
    ax.plot([f[i]], [y[i]], "o", color=colour, markersize=5, markeredgecolor=SURFACE, markeredgewidth=1.5)


def iso_alpha(f, T=20.0, rh=50.0):
    pr, T0, T01 = 101.325, 293.15, 273.16
    Tk = T + 273.15
    h = rh * 10 ** (-6.8346 * (T01 / Tk) ** 1.261 + 4.6151)
    frO = 24 + 4.04e4 * h * (0.02 + h) / (0.391 + h)
    frN = (Tk / T0) ** -0.5 * (9 + 280 * h * np.exp(-4.170 * ((Tk / T0) ** (-1 / 3) - 1)))
    return 8.686 * f**2 * (1.84e-11 * (Tk / T0) ** 0.5 + (Tk / T0) ** -2.5 * (
        0.01275 * np.exp(-2239.1 / Tk) / (frO + f**2 / frO) + 0.1068 * np.exp(-3352.0 / Tk) / (frN + f**2 / frN)))


def main():
    os.makedirs(OUT, exist_ok=True)

    # 1. Air absorption vs ISO 9613-1 (rolloff 0 isolates the spectral part).
    fig, ax = plt.subplots(figsize=(8, 4.2), dpi=130, facecolor=SURFACE)
    for i, dist in enumerate([10, 100, 300, 1000]):
        f, (y,) = response({"distance": dist, "rolloff": 0})
        ax.plot(f[1:], y[1:], color=SERIES[i], linewidth=2, label=f"{dist} m")
        ax.plot(f[1:], -iso_alpha(f[1:]) * (dist - 1), color=MUTED, linewidth=1, linestyle=(0, (3, 3)))
        label_end(ax, f, y, f"{dist} m", SERIES[i], at=2500 if dist >= 300 else 12000)
    ax.plot([], [], color=MUTED, linewidth=1, linestyle=(0, (3, 3)), label="ISO 9613-1 target")
    style(ax, "Air absorption filter (20 °C, 50 % RH)", "Gain (dB)", (-40, 3))
    ax.legend(frameon=False, fontsize=9, labelcolor=INK, loc="lower left")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "air_absorption.png"), facecolor=SURFACE)

    # 2. Occlusion (transmission loss) per material.
    fig, ax = plt.subplots(figsize=(8, 4.2), dpi=130, facecolor=SURFACE)
    for i, (mat, name) in enumerate([(1, "Glass 6 mm"), (3, "Solid wood door"), (4, "Drywall (stud)"), (6, "Concrete 20 cm")]):
        f, (y,) = response({"obstacle": 2, "material": mat})
        ax.plot(f[1:], y[1:], color=SERIES[i], linewidth=2, label=name)
        label_end(ax, f, y, name, SERIES[i], at=[3000, 6000, 1500, 400][i])
    style(ax, "Occlusion: transmission through the partition", "Gain (dB)", (-80, 0))
    ax.legend(frameon=False, fontsize=9, labelcolor=INK, loc="lower left")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "occlusion.png"), facecolor=SURFACE)

    # 3. Headphones: ear responses for a source hard left.
    fig, ax = plt.subplots(figsize=(8, 4.2), dpi=130, facecolor=SURFACE)
    f, (l, r) = response({"renderMode": 1, "pan": -100}, channels=2)
    ax.plot(f[1:], l[1:], color=SERIES[0], linewidth=2, label="Near (left) ear")
    ax.plot(f[1:], r[1:], color=SERIES[1], linewidth=2, label="Far (right) ear")
    label_end(ax, f, l, "near ear", SERIES[0], at=6000)
    label_end(ax, f, r, "far ear", SERIES[1], at=6000)
    style(ax, "Headphones: spherical-head model, source at 90° left", "Gain (dB)", (-18, 12))
    ax.legend(frameon=False, fontsize=9, labelcolor=INK, loc="lower left")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "headphones.png"), facecolor=SURFACE)
    print("figures written to", OUT)


if __name__ == "__main__":
    main()
