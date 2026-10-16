#!/usr/bin/env python
"""Encode WAVs (float32 / int16) to 192 kbps MP3 with lameenc (pip install lameenc).
   python tools/demos/encode_mp3.py <wav-dir> <out-dir>"""
import sys, os, glob
import numpy as np
import lameenc
from scipy.io import wavfile

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for w in sorted(glob.glob(os.path.join(src, "tutorial-*.wav"))):
    sr, x = wavfile.read(w)
    if x.dtype != np.int16:
        x = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    enc = lameenc.Encoder()
    enc.set_bit_rate(192); enc.set_in_sample_rate(sr); enc.set_channels(x.shape[1]); enc.set_quality(2)
    data = enc.encode(x.tobytes()) + enc.flush()
    out = os.path.join(dst, os.path.basename(w)[:-4] + ".mp3")
    open(out, "wb").write(data)
    print(out, len(data), "bytes")
