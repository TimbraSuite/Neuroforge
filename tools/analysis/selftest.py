"""Synthesizes reference signals, runs the analyzer, asserts sanity.
Run: python tools/analysis/selftest.py
"""
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyze as A  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "renders", "selftest")
SR = 44100
DUR = 4.0
F0 = 43.65
BPM = 174.0


def saw(f, n):
    return 2 * ((np.arange(n) * f / SR) % 1.0) - 1


def save(name, x):
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name + ".wav")
    wavfile.write(p, SR, x.astype(np.float32))
    return p


def main():
    n = int(SR * DUR)
    t = np.arange(n) / SR
    s = saw(F0, n)
    static = 0.5 * signal.sosfilt(signal.butter(2, 1500, fs=SR, output="sos"), s)

    rate = BPM / 60 * 2  # 1/8 note = 5.8 Hz
    y = np.zeros(n)
    blk = 64
    zi = np.zeros((1, 2))
    for i in range(0, n, blk):
        lfo = 0.5 - 0.5 * np.cos(2 * np.pi * rate * (i / SR))
        fc = 150 * (2000 / 150) ** lfo
        sos = signal.butter(2, fc, fs=SR, output="sos")
        y[i:i + blk], zi = signal.sosfilt(sos, s[i:i + blk], zi=zi)
    swept = 0.5 * y

    rng = np.random.default_rng(1)
    sub = signal.sosfiltfilt(signal.butter(4, 100, fs=SR, output="sos"), rng.standard_normal((n, 2)), axis=0)
    wide = 0.3 * sub / np.abs(sub).max()
    sine = np.sin(2 * np.pi * 1000 * t)
    sine20 = 0.1 * np.sin(2 * np.pi * 997 * t)

    paths = {"static": save("static_saw", static), "swept": save("swept_saw", swept),
             "wide": save("wide_sub_noise", wide), "fs": save("sine_fs_1k", sine),
             "m20": save("sine_997_m20", sine20)}
    r = {}
    for k, p in paths.items():
        r[k] = A.analyze(p, F0)
        A.print_table(r[k])
        A.make_png(r[k], p[:-4] + ".png")
    A.make_sheet(list(r.values()), os.path.join(OUT, "sheet.png"))
    A.compare(r["static"], r["swept"])

    print("\n--- assertions ---")
    ok = True

    def check(name, cond, info):
        nonlocal ok
        print(("PASS " if cond else "FAIL ") + name + "  " + info)
        ok &= bool(cond)

    check("swept movement > static", r["swept"]["movement_index"] > r["static"]["movement_index"],
          f"{r['swept']['movement_index']:.4f} vs {r['static']['movement_index']:.4f}")
    check("swept mod rate ~5.8 Hz", abs(r["swept"]["mod_rate_hz"] - 5.8) < 0.5,
          f"{r['swept']['mod_rate_hz']:.2f} Hz {r['swept']['mod_note']}")
    check("wide sub low correlation", r["wide"]["sub_corr"] < 0.3, f"{r['wide']['sub_corr']:.3f}")
    check("FS sine LUFS ~ -3.01", abs(r["fs"]["lufs"] + 3.01) < 0.5, f"{r['fs']['lufs']:.2f}")
    check("-20 dBFS sine LUFS ~ -23.01", abs(r["m20"]["lufs"] + 23.01) < 0.5, f"{r['m20']['lufs']:.2f}")
    check("static saw not silent", not r["static"]["silent"], "")
    print("\nALL PASS" if ok else "\nFAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
