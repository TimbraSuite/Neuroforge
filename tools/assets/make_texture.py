"""Seamless 1024x1024 grunge-metal overlay tile for the Neuroforge UI.
RGB = colour, alpha = how strongly it shows. The editor draws it at low opacity on panels."""
import sys
import numpy as np
from PIL import Image

N = 1024
rng = np.random.default_rng(7)

def periodic_noise(sx, sy):
    """Gaussian-filtered white noise via FFT (wraps around, so the tile is seamless)."""
    w = rng.standard_normal((N, N))
    fy = np.fft.fftfreq(N)[:, None]; fx = np.fft.fftfreq(N)[None, :]
    filt = np.exp(-0.5 * ((fx * sx) ** 2 + (fy * sy) ** 2) * (2 * np.pi) ** 2)
    out = np.real(np.fft.ifft2(np.fft.fft2(w) * filt))
    return (out - out.mean()) / (out.std() + 1e-9)

# brushed metal: long horizontal streaks
brushed = 0.6 * periodic_noise(80, 0.6) + 0.4 * periodic_noise(16, 0.4)
# broad mottling
mottle = periodic_noise(120, 120)
lum = 0.5 + 0.10 * brushed + 0.06 * mottle

# scratches: thin bright segments, mostly near-horizontal, wrapped
scr = np.zeros((N, N))
for _ in range(55):
    x0, y0 = rng.uniform(0, N, 2)
    ang = rng.normal(0, 0.35) + (np.pi / 2 if rng.random() < 0.15 else 0)
    length = rng.uniform(12, 70)
    strength = rng.uniform(0.15, 0.55)
    for t in np.linspace(0, length, int(length * 2)):
        x = int(x0 + t * np.cos(ang)) % N; y = int(y0 + t * np.sin(ang)) % N
        scr[y, x] = max(scr[y, x], strength)

# rust: sparse low-frequency blobs with grainy edges
blob = periodic_noise(70, 70) + 0.35 * periodic_noise(6, 6)
rust = np.clip((blob - 1.9) / 0.8, 0, 1)            # only the highest peaks -> a few small patches
grain = np.clip(0.5 + 0.5 * periodic_noise(1.2, 1.2), 0, 1)
rust *= 0.55 + 0.45 * grain

# compose colour: neutral steel, rust in brown-orange
steel = np.dstack([lum * 0.96, lum * 1.0, lum * 1.06])
rust_col = np.dstack([0.55 + 0.15 * grain, 0.26 + 0.08 * grain, 0.10 * np.ones_like(grain)])
rgb = steel * (1 - rust[..., None]) + rust_col * rust[..., None]
rgb = np.clip(rgb + scr[..., None] * 0.35, 0, 1)

# alpha: metal grain shows faintly everywhere, scratches and rust a bit more
alpha = np.clip(0.35 + 0.25 * np.abs(brushed) + 0.5 * scr + 0.9 * rust, 0, 1)
out = np.dstack([rgb, alpha])
Image.fromarray((out * 255).astype(np.uint8), "RGBA").save(sys.argv[1], optimize=True)
print(sys.argv[1], "rust coverage %.1f%%" % (100 * (rust > 0.05).mean()))
