"""Cut the Neuroforge logo out of its black background (alpha from brightness, colour un-premultiplied)."""
import sys
import numpy as np
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
img = np.asarray(Image.open(src).convert("RGB")).astype(np.float32) / 255.0
# black point: the source background is ~0.03-0.05, lift it to transparent
bg = np.percentile(img.max(axis=2), 20)
a = np.clip((img.max(axis=2) - bg) / (1.0 - bg), 0, 1)
a = np.clip(a * 1.6, 0, 1)  # keep solid letters opaque
rgb = np.where(a[..., None] > 1e-3, np.clip((img - bg * (1 - a[..., None])) / np.maximum(a[..., None], 1e-3), 0, 1), 0)
out = np.dstack([rgb, a])
# crop to content
ys, xs = np.where(a > 0.35)
pad = 6
y0, y1 = max(ys.min() - pad, 0), min(ys.max() + pad, a.shape[0])
x0, x1 = max(xs.min() - pad, 0), min(xs.max() + pad, a.shape[1])
out = out[y0:y1, x0:x1]
im = Image.fromarray((out * 255).astype(np.uint8), "RGBA")
# 2x the largest on-screen size (top bar ~46 px high at 1x scale, editor can scale to 2x)
h = 200
im = im.resize((int(im.width * h / im.height), h), Image.LANCZOS)
im.save(dst, optimize=True)
print(dst, im.size, "bg", round(float(bg), 3))
