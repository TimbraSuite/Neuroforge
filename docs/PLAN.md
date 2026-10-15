# Neuroforge — a neurofunk bass instrument

## Goal

Neuroforge is a VST3 synth with one job: neurofunk / neuro DnB bass. It should:

- **Lower the entry.** Choose an archetype and you have a mix-ready neuro bass within seconds: mono sub, gritty mids, movement already wired to macros.
- **Raise the ceiling.** Every stage of the real-world workflow stays exposed and is deeper than doing it by hand: source, pre-filter, distortion, comb/formant, OTT, resample, then repeat.

It is not a "one-knob bass" plugin. It packages the *workflow* (pass → movement → resample → pass again) into the instrument.

## What the research says about the workflow

These points are partly verified; the rest is standard practice.

1. **Source.** Typically one of:
   - FM: sine operators with ratios 0.5, 1, 2 or 3, slightly off-integer for beating. The modulator index moves via a *looping envelope* (Noisia: FM8 "Reptilians"). Several instances are layered with different loop lengths.
   - Reese: 2+ saws at about ±10–30 cents, free-running phase.
   - Wavetable: position sweeps, plus warp (sync, bend, PWM, FM-from-B).
   - Phase distortion: the original Reese came from a Casio CZ.
2. **Pre-filter before distortion.** A notch, band-pass or formant sweep decides *which* harmonics get distorted. This is the "talking" mechanism.
3. **Stacked distortion.** Gentle first (tube or soft), then harder (fold, clip or crush). It is multiband, and the sub stays clean.
4. **Combs, flangers, phasers and allpass dispersion.** High-feedback, key-tracked combs give metallic tones. Disperser-style allpass chains give zaps.
5. **OTT.** 3-band upward + downward compression brings up grit and comb resonances, then EQ follows.
6. **Resample and repeat.** Render, then re-process with *new* movement (different LFO rates). Each pass adds a layer of motion; layering variants adds more.
7. **Mix.** Mono sine sub below about 100–150 Hz (LR4 split). Mids are processed. Width only above about 300 Hz.

## Product concept

```
 ┌──────────── VOICE (poly up to 8, mono/legato default) ─────────────┐
 │ FM OP ──pm──► OSC B ──(FM/Ring/PM B→A)──► OSC A                    │
 │   └──────────pm───────────────────────────►┘                       │
 │ NOISE                                                               │
 │ sum ─► VOICE FILTER (SVF morph / formant) ─► AMP ENV               │
 │ SUB OSC (sine→tri→square) ─────────────────────────────┐(direct)  │
 └─────────────────────────────────────────────────────────┼──────────┘
          │ voice sum (stereo)                             │
          ▼                                                 │
   LOW GUARD (LR4 split @ ~120 Hz) ── low ──► mono ─► SUB BUS (clean / warm sat) ─┐
          │ high                                                                    │
          ▼                                                                         │
   THE FORGE: PASS 1 ─► PASS 2 ─► PASS 3   (each pass = 4 FX slots + MOTION)        │
          ▼                                                                         │
   MID BUS (width, tone) ───────────────────────────── + ◄──────────────────────────┘
          ▼
   MASTER (clipper, gain) ─► out
```

The whole engine runs oversampled internally: 1x, 2x (default) or 4x, using `juce::dsp::Oversampling` with polyphase IIR. Distortion modules also use ADAA where a closed form exists. The result is low aliasing without per-module oversampling complexity.

### Passes and Motion (the resampling metaphor, in real time)

Each **Pass** is 4 FX slots in series plus a dedicated **Motion** modulator. A Motion modulator is a tempo-synced LFO with a shape morph, or a 16-step sequencer with glide, plus a rate offset and a depth.

- **Motion depth** automatically modulates the *primary parameter* (P1) of every slot in the pass: comb frequency, formant vowel, filter cutoff, drive, and so on. Each slot also has a per-slot motion amount.
- Pass 1 might talk at 1/8, pass 2's comb at 1/4 triplet, and pass 3 add a 1/16 shimmer. That is "resample and re-apply movement", minus the bouncing.
- Each pass has a mix control, so it can run as parallel processing.

### Capture (real resampling)

**Capture** renders the current patch offline on a background thread, using a separate engine instance. It holds a note for N beats at the host BPM, with all motion running, and turns the result into a source:

- **Capture → Wavetable.** Pitch-synchronous slicing. The note frequency is known exactly, so no pitch detection is needed. The render becomes up to 256 frames of 2048 samples, phase-aligned to the fundamental and band-limited into mipmaps. It loads as `Captured N` in OSC A or B, where position sweeps scan through the *time* of the old patch's movement. This is the Serum/Vital resample-to-table trick, done in one click.
- **Next Generation.** Captures to OSC A, resets the Forge to a fresh template, and keeps the macros. This is the "now re-process it" step.
- History: 4 capture slots, saved inside the plugin state.

### Modulation

- **Sources:**
  - Env 1 (amp), Env 2, Env 3. Env 2 and Env 3 can **loop**, giving Noisia-style looping FM index envelopes.
  - LFO 1–4: synced or free, shape morph, step mode.
  - Motion 1–3.
  - Macro 1–8.
  - Velocity, note, mod wheel, pitch bend, aftertouch, random-per-note.
- **Mod matrix:** 16 slots, each with source, destination (any continuous parameter), bipolar amount and curve. Modulation happens in the normalized (0..1) parameter domain at sub-block rate (32 samples), and modules smooth internally.
- Voice destinations (osc, voice filter) are modulated per voice. Global destinations (FX) use the newest voice's sources.

### Entry level: Archetypes, Macros, Mutate

- **Archetype generator.** Pick one of: Reese, FM Growl, Talker, Metallic, Tearer, Hoover, Stab, Liquid-Neuro, Donk. Plus **Generate**, which uses seeded randomization inside archetype-safe ranges. Every generated patch wires the 8 macros consistently:
  `GROWL, TALK, GRIT, METAL, MOTION, SPACE/WIDTH, SUB, TONE`.
- **Mutate:** nudges the current patch by a small, medium or wild amount without leaving musical ranges. The sub, levels and Low Guard are protected.
- **Factory presets:** built from archetypes with fixed seeds, then hand-tuned.

### Raise the ceiling

- All internals are exposed: 12 FX slots with 15 module types, a 16-slot matrix and looping envelopes.
- Captures can be fed back into new captures, which makes generations of resamples.
- Module types include the "secret weapons":
  - key-tracked comb
  - formant with shift
  - disperser
  - frequency shifter
  - ring mod
  - OTT with separate up and down amounts
  - bitcrush with downsample

## FX module catalogue (P1 = motion target)

| Type | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| Drive | Drive dB | Shape (soft→hard) | Bias (asym) | Tone |
| Fold | Drive dB | Folds | Bias | Tone |
| Crush | Rate (downsample Hz) | Bits | Jitter | Tone |
| Filter | Cutoff | Reso | Mode morph (LP→BP→HP→Notch) | Drive |
| Formant | Vowel (A-E-I-O-U) | Shift | Q | Spread (stereo) |
| Comb | Freq (or ×note if keytrack) | Feedback ± | Damp | KeyTrack |
| Phaser | Freq | Feedback | Stages | Spread |
| Flanger | Delay ms | Feedback ± | Stereo | Damp |
| FreqShift | Shift Hz ± | Feedback | Stereo | – |
| RingMod | Freq (or ×note) | KeyTrack | Shape | – |
| Disperse | Freq | Amount | Pinch | Spread |
| OTT | Depth | Time | Upward | Downward |
| EQ | Low cut | Tilt | Mid freq | Mid gain |
| Width | Width | Haas | Mono-below | – |

Each slot also has Type, Mix and Motion amount.

## Code layout and ownership

```
src/dsp/osc/       Wavetable bank + mipmaps, table factory, WavetableOsc (unison, warp), FmOp, SubOsc
src/dsp/fx/        FxModule interface + every FX type, Svf, LR4 crossover, Oversampling glue
src/engine/        Params, ModSources (Env/LFO/Motion), ModMatrix, Voice, Engine, Capture, Archetypes
src/ui/            LookAndFeel, panels
src/Plugin*.cpp    AudioProcessor / Editor
src/tests/         test_<area>*.cpp — built by target nf_tests (filter with -DNF_TEST_FILTER=<area>)
tools/render/      nf_render CLI: preset + MIDI riff → WAV (offline, deterministic)
tools/analysis/    analyze.py: spectrogram PNG, alias/DC/NaN/peak/LUFS/sub-mono metrics
```

The DSP is plain C++20 with no JUCE dependency, except `juce::dsp::FFT` for mipmaps. Modules never allocate on the audio thread.

## Verification ("listening")

Claude cannot hear the output, so verification is objective:

1. **Unit tests** per module:
   - stability at extreme params (no NaN or Inf, bounded output)
   - aliasing measurements (inharmonic energy on a sustained high note)
   - frequency-response checks (filters, crossover sums flat)
2. **Offline renders.** A neuro riff at 174 BPM, in F, with each preset → WAV.
3. **Analysis:**
   - spectrograms reviewed visually
   - sub band mono correlation
   - peak/RMS/crest
   - spectral centroid motion: is there movement?
   - harmonic density in 200 Hz–5 kHz
   - DC offset
   - CPU real-time factor
4. **The user's ears** are the final judge. Renders are written to `renders/` for listening.

## Milestones

1. Skeleton builds ✅ (JUCE 8.0.4, VS 2026, CMake)
2. Oscillators, FX modules, crossover — parallel agents, with unit tests
3. Engine: voices, modulation, passes, Low Guard, oversampling, params, state
4. Renderer + analysis tooling; first renders
5. UI: dark, focused; archetype bar, source panel, Forge (3 passes), modulation, macros
6. Archetypes, factory presets, Mutate, Capture
7. Iterate on sound with renders and metrics; tune the presets
