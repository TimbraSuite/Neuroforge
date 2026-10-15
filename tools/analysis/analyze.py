#!/usr/bin/env python
"""Offline audio analysis for synthesized neurofunk bass renders.

  python tools/analysis/analyze.py a.wav b.wav --out renders/analysis [--sheet sheet.png] [--note-hz 43.65]
  python tools/analysis/analyze.py --compare a.wav b.wav [--note-hz 43.65]
"""
import argparse
import json
import math
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

BPM = 174.0
BANDS = [("sub", 20, 90), ("low", 90, 250), ("lowmid", 250, 800),
         ("mid", 800, 2500), ("highmid", 2500, 6000), ("air", 6000, 20000)]
NFFT = 2048
HOP = 256


# ----------------------------------------------------------------- io
def read_wav(path):
    sr, x = wavfile.read(path)
    if x.dtype == np.uint8:
        x = (x.astype(np.float64) - 128.0) / 128.0
    elif x.dtype == np.int16:
        x = x.astype(np.float64) / 32768.0
    elif x.dtype == np.int32:  # 24-bit is returned left-justified in int32
        x = x.astype(np.float64) / 2147483648.0
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    return sr, x  # (n, ch)


def db(v, floor=-200.0):
    return float(max(floor, 10 * math.log10(v))) if v > 0 else floor


def db20(v, floor=-200.0):
    return float(max(floor, 20 * math.log10(v))) if v > 0 else floor


# ----------------------------------------------------------------- loudness
def k_weight_sos(fs):
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / fs)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / fs)
    a0 = 1 + K / Q + K * K
    b2 = [1, -2, 1]
    a2 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    return [signal.tf2sos(b1, a1), signal.tf2sos(b2, a2)]


def lufs_integrated(x, fs):
    n = x.shape[0]
    zs = []
    for c in range(x.shape[1]):
        y = x[:, c]
        for sos in k_weight_sos(fs):
            y = signal.sosfilt(sos, y)
        zs.append(y * y)
    z = np.stack(zs, 1)
    blk = int(round(0.4 * fs))
    step = int(round(0.1 * fs))
    if n < blk:
        ms = z.mean(0)[None, :]
    else:
        cs = np.vstack([np.zeros((1, z.shape[1])), np.cumsum(z, 0)])
        starts = np.arange(0, n - blk + 1, step)
        ms = (cs[starts + blk] - cs[starts]) / blk
    p = ms.sum(1)  # channel weight 1.0 for L/R (mono counts as one channel)
    l = -0.691 + 10 * np.log10(np.maximum(p, 1e-20))
    g = p[l > -70]
    if g.size == 0:
        return -70.0
    rel = -0.691 + 10 * math.log10(g.mean()) - 10
    g2 = p[(l > -70) & (l > rel)]
    if g2.size == 0:
        return -70.0
    return float(-0.691 + 10 * math.log10(g2.mean()))


# ----------------------------------------------------------------- helpers
def corr(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = math.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 1e-20 else 1.0


def side_mid_db(l, r):
    m = (l + r) / 2
    s = (l - r) / 2
    em, es = float((m * m).sum()), float((s * s).sum())
    if em <= 1e-30:
        return 0.0
    return max(-120.0, db(es / em)) if es > 0 else -120.0


def lp_hp(x, fs, fc, kind):
    if fc >= fs / 2 or x.size < 64:
        return x
    sos = signal.butter(4, fc, btype=kind, fs=fs, output="sos")
    return signal.sosfiltfilt(sos, x)


def note_label(rate_hz):
    if rate_hz <= 0:
        return "n/a"
    period_beats = (BPM / 60.0) / rate_hz  # cycle length in quarter notes
    names = {4.0: "1 bar", 2.0: "1/2", 1.0: "1/4", 0.5: "1/8", 0.25: "1/16", 0.125: "1/32",
             0.0625: "1/64", 1.0 / 3: "1/4T", 1.0 / 6: "1/8T", 1.0 / 12: "1/16T",
             0.75: "1/8.", 1.5: "1/4.", 0.375: "1/16."}
    best = min(names, key=lambda k: abs(math.log(period_beats / k)))
    err = abs(math.log(period_beats / best))
    pre = "=" if err < 0.06 else "~"
    return f"{pre}{names[best]} note ({period_beats:.3f} beats/cycle)"


def stft_mag(x, fs):
    if x.size < NFFT:
        x = np.pad(x, (0, NFFT - x.size))
    f, t, Z = signal.stft(x, fs=fs, window="hann", nperseg=NFFT, noverlap=NFFT - HOP,
                          boundary=None, padded=False)
    return f, t, np.abs(Z) * (NFFT / 2)


# ----------------------------------------------------------------- analysis
def analyze(path, note_hz=None):
    fs, x = read_wav(path)
    n, ch = x.shape
    M = {"file": os.path.basename(path), "sample_rate": fs, "channels": ch,
         "duration_s": n / fs}
    bad = ~np.isfinite(x)
    M["nan_inf_count"] = int(bad.sum())
    x = np.where(bad, 0.0, x)
    L = x[:, 0]
    R = x[:, 1] if ch > 1 else x[:, 0]
    mono = x.mean(1)

    peak = float(np.abs(x).max()) if n else 0.0
    up = signal.resample_poly(x, 4, 1, axis=0) if n else x
    tp = float(np.abs(up).max()) if n else 0.0
    rms = math.sqrt(float((x ** 2).mean())) if n else 0.0
    M["peak_dbfs"] = db20(peak)
    M["true_peak_dbfs"] = db20(tp)
    M["rms_dbfs"] = db20(rms)
    M["crest_db"] = M["peak_dbfs"] - M["rms_dbfs"] if rms > 0 else 0.0
    M["lufs"] = lufs_integrated(x, fs)
    M["dc_offset"] = [float(x[:, c].mean()) for c in range(ch)]
    M["clipped_pct"] = float((np.abs(x) >= 1.0).mean() * 100)
    M["silent"] = bool(M["rms_dbfs"] < -60)

    # stereo
    lo_l, lo_r = lp_hp(L, fs, 120, "low"), lp_hp(R, fs, 120, "low")
    hi_l, hi_r = lp_hp(L, fs, 300, "high"), lp_hp(R, fs, 300, "high")
    M["sub_corr"] = corr(lo_l, lo_r)
    M["sub_side_mid_db"] = side_mid_db(lo_l, lo_r)
    M["high_corr"] = corr(hi_l, hi_r)
    M["high_side_mid_db"] = side_mid_db(hi_l, hi_r)

    # bands (Welch, summed over channels)
    nper = min(8192, max(256, n))
    psd = 0
    for c in range(ch):
        fw, p = signal.welch(x[:, c], fs=fs, nperseg=nper, window="hann")
        psd = psd + p
    df = fw[1] - fw[0]
    tot = psd[(fw >= 20) & (fw < 20000)].sum() * df
    M["bands"] = {}
    for name, a, b in BANDS:
        e = psd[(fw >= a) & (fw < b)].sum() * df
        M["bands"][name] = {"pct": float(100 * e / tot) if tot > 0 else 0.0, "db": db(e)}

    # STFT-based movement
    f, t, S = stft_mag(mono, fs)
    frame_e = (S ** 2).sum(0)
    fdb = 10 * np.log10(frame_e + 1e-20)
    active = fdb > (fdb.max() - 50)
    cm = (f >= 20) & (f <= 20000)
    cen = (S[cm] * f[cm, None]).sum(0) / (S[cm].sum(0) + 1e-20)
    ca = cen[active] if active.any() else cen
    M["centroid_mean_hz"] = float(ca.mean())
    M["centroid_std_hz"] = float(ca.std())
    M["centroid_cv"] = M["centroid_std_hz"] / M["centroid_mean_hz"] if M["centroid_mean_hz"] > 0 else 0.0
    mag_n = S / (S.max() + 1e-20)
    flux = np.sqrt((np.diff(mag_n, axis=1) ** 2).sum(0)) if S.shape[1] > 1 else np.zeros(1)
    M["flux_mean"] = float(flux.mean())
    mm = (f >= 100) & (f <= 8000)
    lm = 20 * np.log10(S[mm] + 1e-12)
    ref = 20 * np.log10(S.max() + 1e-12)
    lm = np.clip(lm - (ref - 80), 0, None)  # dB above an 80 dB floor
    num = (lm[:, 1:] * lm[:, :-1]).sum(0)
    den = np.linalg.norm(lm[:, 1:], axis=0) * np.linalg.norm(lm[:, :-1], axis=0) + 1e-20
    cd = 1 - num / den
    pair_ok = active[1:] & active[:-1] if active.size > 1 else np.ones(cd.shape, bool)
    M["movement_index"] = float(cd[pair_ok].mean()) if pair_ok.any() else 0.0

    # modulation rate of centroid trajectory
    fr = fs / HOP
    mod_hz, mod_amp = 0.0, 0.0
    if active.sum() > 16:
        idx = np.arange(cen.size)
        c2 = np.interp(idx, idx[active], cen[active])
        c2 = signal.detrend(c2, type="linear")
        w = np.hanning(c2.size)
        nfft = 1 << int(math.ceil(math.log2(c2.size * 8)))
        sp = np.abs(np.fft.rfft(c2 * w, nfft))
        fr_ax = np.fft.rfftfreq(nfft, 1 / fr)
        sel = (fr_ax >= 0.5) & (fr_ax <= 30)
        if sel.any() and sp[sel].max() > 0:
            k = np.argmax(np.where(sel, sp, 0))
            mod_hz = float(fr_ax[k])
            mod_amp = float(sp[k] * 2 / w.sum())  # approx amplitude of component in Hz
    M["mod_rate_hz"] = mod_hz
    M["mod_depth_hz"] = mod_amp
    M["mod_note"] = note_label(mod_hz) if mod_amp > 0.005 * max(M["centroid_mean_hz"], 1) else "none"

    # harmonic density (long-term average spectrum, 200-5k)
    bm = (fw >= 200) & (fw <= 5000)
    pb = psd[bm]
    if pb.size > 3 and pb.max() > 0:
        pdb = 10 * np.log10(pb + 1e-30)
        pk, _ = signal.find_peaks(pdb, height=pdb.max() - 40, prominence=6)
        M["density_peaks"] = int(pk.size)
        M["flatness"] = float(np.exp(np.mean(np.log(pb + 1e-30))) / (pb.mean() + 1e-30))
    else:
        M["density_peaks"], M["flatness"] = 0, 0.0

    if note_hz:
        sel = (fw >= 100) & (fw <= 10000)
        ff, pp = fw[sel], psd[sel]
        k = np.maximum(np.round(ff / note_hz), 1)
        dist = np.abs(ff - k * note_hz)
        M["inharmonic_frac"] = float(pp[dist > note_hz / 4].sum() / (pp.sum() + 1e-30))
    else:
        M["inharmonic_frac"] = None

    warn = []
    if M["silent"]:
        warn.append("SILENT (RMS < -60 dBFS)")
    if M["nan_inf_count"]:
        warn.append("NaN/Inf present")
    if M["clipped_pct"] > 0:
        warn.append("clipping")
    if M["true_peak_dbfs"] > 0:
        warn.append("true-peak > 0 dBFS")
    M["warnings"] = warn

    # non-JSON data for plotting
    M["_x"], M["_fs"] = x, fs
    M["_spec"] = log_spec(mono, fs)
    M["_cen"] = (t, cen, active)
    M["_psd"] = (fw, psd)
    return M


def log_spec(mono, fs, rows=300, max_cols=1600):
    nseg = 4096
    hop = max(256, int(len(mono) / max_cols))
    if mono.size < nseg:
        mono = np.pad(mono, (0, nseg - mono.size))
    f, t, Z = signal.stft(mono, fs=fs, window="hann", nperseg=nseg, noverlap=nseg - hop,
                          nfft=8192, boundary=None, padded=False)
    P = 20 * np.log10(np.abs(Z) + 1e-9)
    fo = np.geomspace(20, min(20000, fs / 2 * 0.999), rows)
    ix = np.clip(np.searchsorted(f, fo), 1, len(f) - 1)
    w = (fo - f[ix - 1]) / (f[ix] - f[ix - 1])
    img = P[ix - 1] * (1 - w)[:, None] + P[ix] * w[:, None]
    return fo, t + nseg / 2 / fs, img


# ----------------------------------------------------------------- output
ROWS = [
    ("duration_s", "duration (s)", "{:.2f}"),
    ("peak_dbfs", "peak dBFS", "{:.2f}"),
    ("true_peak_dbfs", "true peak dBFS (4x)", "{:.2f}"),
    ("rms_dbfs", "RMS dBFS", "{:.2f}"),
    ("crest_db", "crest (dB)", "{:.2f}"),
    ("lufs", "LUFS integrated", "{:.2f}"),
    ("clipped_pct", "clipped %", "{:.4f}"),
    ("nan_inf_count", "NaN/Inf count", "{}"),
    ("sub_corr", "sub (<120) L/R corr", "{:.3f}"),
    ("sub_side_mid_db", "sub side/mid dB", "{:.1f}"),
    ("high_corr", "high (>300) L/R corr", "{:.3f}"),
    ("high_side_mid_db", "high side/mid dB", "{:.1f}"),
    ("centroid_mean_hz", "centroid mean Hz", "{:.0f}"),
    ("centroid_std_hz", "centroid std Hz", "{:.0f}"),
    ("centroid_cv", "centroid std/mean", "{:.3f}"),
    ("flux_mean", "spectral flux mean", "{:.4f}"),
    ("movement_index", "MOVEMENT INDEX", "{:.4f}"),
    ("mod_rate_hz", "centroid mod rate Hz", "{:.2f}"),
    ("mod_depth_hz", "centroid mod depth Hz", "{:.0f}"),
    ("density_peaks", "harmonic peaks 200-5k", "{}"),
    ("flatness", "flatness 200-5k", "{:.4f}"),
    ("inharmonic_frac", "inharmonic frac", "{:.3f}"),
]


def fmt(M, key, f):
    v = M.get(key)
    return "n/a" if v is None else f.format(v)


def flat(M):
    d = {k: M[k] for k, _, _ in ROWS if isinstance(M.get(k), (int, float))}
    for b, v in M["bands"].items():
        d[f"band_{b}_pct"] = v["pct"]
        d[f"band_{b}_db"] = v["db"]
    d["dc_offset_max"] = max(abs(v) for v in M["dc_offset"])
    return d


def print_table(M):
    print(f"\n=== {M['file']}  ({M['sample_rate']} Hz, {M['channels']} ch) ===")
    for k, lab, f in ROWS:
        print(f"  {lab:<26}{fmt(M, k, f):>12}")
    print(f"  {'DC offset':<26}{', '.join(f'{v:+.5f}' for v in M['dc_offset']):>12}")
    print(f"  {'modulation note':<26}{M['mod_note']}")
    print("  bands: " + "  ".join(f"{b} {v['pct']:.1f}% ({v['db']:.0f}dB)" for b, v in M["bands"].items()))
    if M["warnings"]:
        print("  WARNINGS: " + "; ".join(M["warnings"]))


def clean(M):
    return {k: v for k, v in M.items() if not k.startswith("_")}


def _style():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"figure.facecolor": "#0e0e14", "axes.facecolor": "#14141c",
                         "axes.edgecolor": "#33333f", "axes.labelcolor": "#d8d8e0",
                         "xtick.color": "#d8d8e0", "ytick.color": "#d8d8e0", "text.color": "#d8d8e0",
                         "font.size": 9, "grid.color": "#33333f"})
    return plt


def make_png(M, path):
    plt = _style()
    from matplotlib import gridspec
    bg, fg, grid = "#0e0e14", "#d8d8e0", "#33333f"
    x, fs = M["_x"], M["_fs"]
    n = x.shape[0]
    dur = n / fs
    fig = plt.figure(figsize=(16, 10), dpi=100)
    gs = gridspec.GridSpec(3, 3, height_ratios=[1.0, 2.6, 1.4], width_ratios=[1, 1, 0.62],
                           hspace=0.32, wspace=0.18, left=0.055, right=0.985, top=0.94, bottom=0.06)
    ax = fig.add_subplot(gs[0, :2])
    nb = max(1, min(2000, n))
    edges = np.linspace(0, n, nb + 1).astype(int)
    cols = ["#ff9f43", "#4cc9f0"]
    for c in range(min(2, x.shape[1])):
        mn = np.array([x[a:b, c].min() if b > a else 0 for a, b in zip(edges[:-1], edges[1:])])
        mx = np.array([x[a:b, c].max() if b > a else 0 for a, b in zip(edges[:-1], edges[1:])])
        ax.fill_between(edges[:-1] / fs, mn, mx, color=cols[c], alpha=0.55 if x.shape[1] > 1 else 0.9, lw=0)
    ax.set_xlim(0, dur)
    ax.set_ylim(-1.05, 1.05)
    ax.set_title(f"{M['file']}   waveform" + (" (L orange / R blue)" if x.shape[1] > 1 else ""), loc="left")
    ax.grid(True, alpha=0.3)

    fo, ts, img = M["_spec"]
    ax2 = fig.add_subplot(gs[1, :2], sharex=ax)
    vmax = img.max()
    ax2.pcolormesh(ts, fo, img, cmap="magma", vmin=vmax - 80, vmax=vmax, shading="nearest", rasterized=True)
    ax2.set_yscale("log")
    ax2.set_ylim(20, 20000)
    ax2.set_yticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000])
    ax2.set_yticklabels(["20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"])
    ax2.minorticks_off()
    for _, a, _b in BANDS[1:]:
        ax2.axhline(a, color="white", lw=0.4, alpha=0.3, ls="--")
    ax2.set_ylabel("Hz")
    ax2.set_title("spectrogram (log-f, 80 dB range)", loc="left")

    tc_, cen, act = M["_cen"]
    ax3 = fig.add_subplot(gs[2, :2], sharex=ax)
    ax3.plot(tc_, cen, color="#5eead4", lw=0.8)
    ax3.set_yscale("log")
    ax3.set_ylim(max(20, np.percentile(cen, 1) * 0.5), min(20000, max(cen.max() * 1.5, 100)))
    ax3.set_ylabel("Hz")
    ax3.set_xlabel("time (s)")
    ax3.set_title(f"spectral centroid   mod {M['mod_rate_hz']:.2f} Hz, {M['mod_note']}", loc="left")
    ax3.grid(True, alpha=0.3, which="both")

    fw, p = M["_psd"]
    ax4 = fig.add_subplot(gs[1, 2])
    m = (fw >= 20) & (fw <= 20000)
    pdb = 10 * np.log10(p[m] + 1e-30)
    ax4.plot(fw[m], pdb, color="#f472b6", lw=0.8)
    ax4.set_xscale("log")
    ax4.set_xlim(20, 20000)
    top = pdb.max()
    ax4.set_ylim(top - 90, top + 3)
    for name, a, b in BANDS:
        ax4.axvline(a, color="white", lw=0.5, alpha=0.4, ls="--")
        ax4.text(math.sqrt(a * b), top - 2, name, ha="center", va="top", fontsize=7, color="#bbbbcc")
    ax4.set_xticks([20, 100, 1000, 10000])
    ax4.set_xticklabels(["20", "100", "1k", "10k"])
    ax4.minorticks_off()
    ax4.set_title("long-term average spectrum (dB)", loc="left")
    ax4.grid(True, alpha=0.3)

    ax5 = fig.add_subplot(gs[2, 2])
    ax5.axis("off")
    lines = [("LUFS", "lufs", "{:.1f}"), ("peak / TP dBFS", None, None), ("RMS dBFS", "rms_dbfs", "{:.1f}"),
             ("crest dB", "crest_db", "{:.1f}"), ("sub corr", "sub_corr", "{:.2f}"),
             ("sub S/M dB", "sub_side_mid_db", "{:.1f}"), ("hi width S/M dB", "high_side_mid_db", "{:.1f}"),
             ("movement idx", "movement_index", "{:.4f}"), ("centroid CV", "centroid_cv", "{:.2f}"),
             ("mod rate Hz", "mod_rate_hz", "{:.2f}"), ("harm. peaks", "density_peaks", "{}"),
             ("flatness", "flatness", "{:.3f}"), ("inharmonic", "inharmonic_frac", "{:.2f}")]
    txt = []
    for lab, k, f in lines:
        if k is None:
            txt.append(f"{lab:<16}{M['peak_dbfs']:.1f} / {M['true_peak_dbfs']:.1f}")
        else:
            txt.append(f"{lab:<16}{fmt(M, k, f)}")
    txt.append("")
    txt.append("bands %: " + " ".join(f"{b[:3]}{v['pct']:.0f}" for b, v in M["bands"].items()))
    if M["warnings"]:
        txt.append("!! " + "; ".join(M["warnings"]))
    ax5.text(0.0, 1.0, "\n".join(txt), va="top", ha="left", family="monospace", fontsize=9,
             color="#ffd166" if M["warnings"] else fg, bbox=dict(boxstyle="round", fc="#1b1b26", ec=grid))
    fig.savefig(path, facecolor=bg)
    plt.close(fig)


def make_sheet(results, path):
    plt = _style()
    bg, fg = "#0e0e14", "#d8d8e0"
    n = len(results)
    fig = plt.figure(figsize=(16, max(2.2, 1.35 * n + 0.5)), dpi=100, facecolor=bg)
    h = 0.94 / n
    for i, M in enumerate(results):
        top = 0.97 - i * h
        ax = fig.add_axes([0.04, top - h * 0.88, 0.62, h * 0.82])
        fo, ts, img = M["_spec"]
        v = img.max()
        ax.pcolormesh(ts, fo, img, cmap="magma", vmin=v - 80, vmax=v, shading="nearest", rasterized=True)
        ax.set_yscale("log")
        ax.set_ylim(20, 20000)
        ax.set_yticks([100, 1000, 10000])
        ax.set_yticklabels(["100", "1k", "10k"])
        ax.minorticks_off()
        ax.tick_params(colors=fg, labelsize=7)
        ta = fig.add_axes([0.68, top - h * 0.88, 0.31, h * 0.82])
        ta.axis("off")
        warn = ("!! " + "; ".join(M["warnings"])) if M["warnings"] else ""
        ta.text(0, 1, M["file"], color="#ffd166" if warn else fg, fontsize=10, va="top", weight="bold")
        ta.text(0, 0.62, f"LUFS {M['lufs']:.1f}    movement {M['movement_index']:.4f}\n"
                         f"sub corr {M['sub_corr']:.2f}    density {M['density_peaks']} peaks\n"
                         f"mod {M['mod_rate_hz']:.1f} Hz {M['mod_note'].split(' (')[0]}\n{warn}",
                color=fg, fontsize=9, va="top", family="monospace")
    fig.savefig(path, facecolor=bg)
    plt.close(fig)


def compare(a, b):
    fa, fb = flat(a), flat(b)
    print(f"\n=== COMPARE  A={a['file']}  B={b['file']} ===")
    print(f"  {'metric':<24}{'A':>12}{'B':>12}{'B-A':>12}")
    for k in fa:
        if k in fb:
            print(f"  {k:<24}{fa[k]:>12.4f}{fb[k]:>12.4f}{fb[k] - fa[k]:>+12.4f}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--out", default="renders/analysis")
    ap.add_argument("--sheet")
    ap.add_argument("--note-hz", type=float)
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"))
    ap.add_argument("--no-png", action="store_true")
    a = ap.parse_args(argv)
    if a.compare:
        compare(analyze(a.compare[0], a.note_hz), analyze(a.compare[1], a.note_hz))
        if not a.files:
            return 0
    if not a.files:
        ap.error("no input files")
    os.makedirs(a.out, exist_ok=True)
    results = []
    for p in a.files:
        M = analyze(p, a.note_hz)
        base = os.path.splitext(os.path.basename(p))[0]
        print_table(M)
        with open(os.path.join(a.out, base + ".json"), "w") as fh:
            json.dump(clean(M), fh, indent=2)
        if not a.no_png:
            make_png(M, os.path.join(a.out, base + ".png"))
        results.append(M)
    if a.sheet:
        sp = a.sheet if os.path.dirname(a.sheet) else os.path.join(a.out, a.sheet)
        os.makedirs(os.path.dirname(os.path.abspath(sp)), exist_ok=True)
        make_sheet(results, sp)
        print(f"\nsheet -> {sp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
