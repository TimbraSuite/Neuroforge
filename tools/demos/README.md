# Tutorial demos

Renders the end result of the three tutorials in `docs/MANUAL.md` as ~11 s, 174 BPM basslines
(8 bars, no drums) with the engine-only renderer `nf_demo` (`demo.cpp`). The patches are built
from parameter settings that mirror the tutorial steps; tutorial 3 runs the real Capture /
RESAMPLE chain (`renderCapture` + `nextGenerationState`) twice.

```
cmake -S . -B build-eng
cmake --build build-eng --config Release --target nf_demo
build-eng/Release/nf_demo.exe out_dir            # tutorial-1.wav .. tutorial-3.wav (stereo float32, 48 kHz, -1 dBFS peak)
build-eng/Release/nf_demo.exe out_dir 2          # only tutorial 2
pip install lameenc scipy numpy
python tools/demos/encode_mp3.py out_dir docs/audio   # 192 kbps MP3s
python tools/analysis/analyze.py out_dir/tutorial-*.wav --out out_dir/analysis   # spectrogram check
```

Edit the `tutorialN()` functions to change a patch and `lineN()` to change the notes.
