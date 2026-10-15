<p align="center">
  <img src="docs/images/logo.png" alt="Neuroforge" width="520">
</p>

<p align="center">
  <b>A neurofunk bass instrument.</b> Great neuro bass in seconds, and deep enough to spend hours in.<br>
  VST3 + Standalone · Windows x64 · <b>beta</b>
</p>

<p align="center">
  <a href="https://github.com/huuba91/neuroforge/releases/tag/v0.1.0-beta"><b>⬇ Download v0.1.0-beta</b></a> ·
  <a href="docs/MANUAL.md"><b>📖 Manual & tutorials</b></a> ·
  <a href="https://github.com/huuba91/neuroforge/issues">Report a bug</a>
</p>

![Neuroforge screenshot](docs/images/screenshot.png)

## What it is

Neurofunk basses (think Noisia, Phace, Ed Rush & Optical, Emperor, The Upbeats, Teddy Killerz) usually take a long chain of synths, effects and resampling passes. Neuroforge builds that workflow into one instrument:

> source → pre-filter → stacked distortion → comb / formant / phaser → OTT → **resample → do it again**

It does not dumb the process down. It lowers the entry point and raises the ceiling.

## Features

- **FORGE.** One click generates a mix-ready patch for one of 9 archetypes: Reese, FM Growl, Talker, Metallic, Tearer, Hoover, Stab, Liquid and Donk. Each click is a new variation at a consistent loudness.
- **8 macros with fixed meanings:** GROWL, TALK, GRIT, METAL, MOTION, WIDTH, SUB and TONE. They do the same job on every generated patch.
- **MUTATE** varies the current sound (subtle, medium or wild) while keeping it musical.
- **The Forge:** 3 passes × 4 FX slots, choosing from 14 effect types.
  - Distortion and filtering: Drive and Fold (both antialiased with ADAA), Crush, Filter, Formant and Comb.
  - Modulation effects: Phaser, Flanger, Frequency Shifter, Ring Mod and Disperser-style dispersion.
  - Dynamics and finishing: 3-band OTT, EQ and Width.
  - Each pass has its own **Motion**, a tempo-synced LFO or a 16-step sequencer. Each pass re-applies movement the way a resample pass would.
- **In-plugin resampling.**
  - **CAPTURE** renders your patch into a wavetable, sliced in sync with the note's pitch.
  - **NEXT GEN** loads that wavetable into a clean Forge so you can process it again, for several generations.
- **Low Guard** keeps the sub clean and mono. An LR4 split sends the lows to a mono sub bus, and only the mids get destroyed.
- **Source:** two wavetable oscillators and an FM operator.
  - 12 procedural neuro wavetables.
  - Unison up to 16 voices.
  - 11 warp modes.
- **Modulation:**
  - Looping envelopes, for Noisia-style self-moving FM.
  - 4 LFOs, 24 matrix slots and drag-to-modulate on every knob.
- 2x/4x internal oversampling with linear-phase decimation.

## Install (beta)

1. Download `Neuroforge-0.1.0-beta-win64.zip` from the [release page](https://github.com/huuba91/neuroforge/releases/tag/v0.1.0-beta).
2. Copy the **`Neuroforge.vst3`** folder to `C:\Program Files\Common Files\VST3\`.
3. Rescan plug-ins in your DAW. In Ableton: *Settings → Plug-ins → Use VST3 Plug-in System Folders → Rescan*.
4. Neuroforge appears as an instrument. Play around **F1–F2**.

`Neuroforge.exe` is a standalone version, if you want to try it without a DAW.

> The binaries are not code-signed yet, so Windows SmartScreen may warn on first launch.

## Learn it

The **[Manual](docs/MANUAL.md)** covers:
- a 2-minute quick start
- a signal-flow diagram
- every panel and effect
- three step-by-step tutorials: a self-moving FM growl, a talking vowel bass, and two generations of resampling

## Beta status

This is the first public beta. Here is what we know:
- It has only been tested on Windows with Ableton Live. Please report other DAWs.
- Presets are tuned for basslines around F1–F2 (about 44–87 Hz).
- Heavily clipped patches can overshoot 0 dBTP by a fraction of a dB between samples. A master limiter handles that.
- Preset and plugin-state formats may still change before 1.0.

Feedback, bug reports and sound-design ideas are very welcome in [Issues](https://github.com/huuba91/neuroforge/issues).

## Build from source

Requirements: Windows, Visual Studio 2022/2026 (C++ workload) and CMake ≥ 3.22.

```bash
git clone https://github.com/huuba91/neuroforge.git
cd neuroforge
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git libs/JUCE
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target Neuroforge_VST3 Neuroforge_Standalone
```

The output lands in `build/Neuroforge_artefacts/Release/`.

### Project layout

| Path | What |
| --- | --- |
| `src/dsp/` | Pure C++20 DSP, with no JUCE: wavetable oscillators, FX modules, crossovers |
| `src/engine/` | Voices, modulation, Forge passes, Low Guard, archetypes, Capture |
| `src/ui/`, `src/Plugin*` | JUCE plugin wrapper and interface |
| `src/tests/` | `nf_tests`: stability fuzzing, aliasing, crossover flatness, determinism, capture |
| `tools/render/` | `nf_render`: deterministic offline renders of any patch to WAV |
| `tools/analysis/` | `analyze.py`: spectrograms, LUFS, sub mono-ness, movement metrics, contact sheets |
| `tools/assets/` | Scripts that generate the logo cut-out and UI texture |
| `docs/` | [Manual](docs/MANUAL.md) and [design notes](docs/PLAN.md) |

## License

Neuroforge is released under the **GNU AGPLv3**; see [LICENSE](LICENSE). It is built with [JUCE](https://juce.com), which is used under its AGPLv3 option.
