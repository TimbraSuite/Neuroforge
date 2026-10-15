# Neuroforge

Neuroforge is a VST3 instrument for neurofunk and neuro DnB basses. It builds in the real workflow:
- a source
- pre-filtering
- stacked distortion
- comb, formant, phaser and dispersion
- OTT
- resampling, then going round again

The goal: a great bass in seconds, with every stage still open for hours of deep sound design.

The design rationale and research notes are in [docs/PLAN.md](docs/PLAN.md).

## The idea in one screen

| Area | What it does |
|---|---|
| **Archetype + FORGE** | Generates a mix-ready patch for one of 9 archetypes: Reese, FM Growl, Talker, Metallic, Tearer, Hoover, Stab, Liquid, Donk. Patches are seeded, so the same seed gives the same patch. Each one is auto-gained to a consistent loudness. |
| **Macros** | The same 8 meanings on every generated patch: GROWL, TALK, GRIT, METAL, MOTION, WIDTH, SUB, TONE. |
| **Mutate** | Nudges the patch subtly, medium or wildly while staying musical. Levels, sub and Low Guard are protected. |
| **Source** | Two wavetable oscillators: 12 procedural neuro tables, unison up to 16, 10 warp modes, B→A FM/ring. A sine-family FM operator. A sub. Noise. A morphing voice filter. A pitch envelope. |
| **The Forge** | 3 passes × 4 FX slots, with 14 module types. Each pass has its own **Motion** (synced LFO or 16-step sequencer) that drives the main parameter of every slot in that pass. That is "re-apply movement after resampling", without bouncing audio. |
| **Capture / Next Gen** | Renders the patch offline and slices it pitch-synchronously into a wavetable (up to 256 frames) that you can play on Osc A/B. Scanning the position moves through the *time* of the old patch. Next Gen loads the capture into a clean Forge so you can process the resample again. Captures of captures work. |
| **Low Guard** | An LR4 split (around 120 Hz). The lows go to a mono sub bus (clean or warm). The mids go through the Forge, then get high-passed again so distortion can't muddy the sub. |
| **Modulation** | 3 envelopes (Env 2/3 can **loop**, for Noisia-style looping FM index), 4 LFOs, 3 Motions, 8 macros, velocity, note, wheel and so on. 24 matrix slots. Drag a source onto any knob to modulate it. |

The engine runs internally at 2x oversampling by default (1x/2x/4x selectable) and decimates with linear-phase halfband FIRs. Drive and Fold use ADAA.

## Build (Windows, VS 2026, CMake ≥ 3.22)

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git libs/JUCE
```

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
```

```bash
cmake --build build --config Release --target Neuroforge_VST3 Neuroforge_Standalone
```

The VST3 lands in `build/Neuroforge_artefacts/Release/VST3/Neuroforge.vst3`. To install it, copy it to `C:\Program Files\Common Files\VST3`.

## Developer tools

| Target / script | Purpose |
|---|---|
| `nf_tests` | Unit tests for every DSP module and the engine: stability fuzzing, aliasing, crossover flatness, determinism, capture. |
| `nf_render` | Deterministic offline renders: `nf_render --all renders/all`, `nf_render --arch "FM Growl" --seed 3 --out x.wav --riff hold`, `--set id=value` overrides. |
| `tools/analysis/analyze.py` | Spectrogram PNGs, LUFS/true-peak, sub mono-ness, band balance, movement index, modulation rate, harmonic density and a contact sheet across presets. |

The DSP (`src/dsp`) and engine (`src/engine`) are plain C++20 with no JUCE dependency. The plugin and UI live in `src/Plugin*` and `src/ui`.
