# DynEQ — 4-band parallel dynamic EQ (VST3)

A low-CPU, zero-latency dynamic EQ inspired by the workflow of TDR Nova and the
curve of FL Studio's Fruity Parametric EQ 2. Four parametric bands, each with its
own parallel *mix* and per-band compression, a live spectrum analyser, and a
drag-the-node curve editor.

Cross-platform **VST3** (Linux / macOS / Windows). No installer — just drop the
`DynEQ.vst3` bundle into your VST3 folder.

## What's inside

- **4 bands** (Bell / Low Shelf / High Shelf), 20 Hz – 20 kHz.
- **Parallel mix** per band — the band's effect is added back onto the dry
  signal, so you can dial in "a little of the band" like Nova.
- **Per-band dynamics**: threshold, ratio, max reduction (range), attack and
  release, keyed to the band's own content (stereo-linked).
- **Static + dynamic** gain combine: the dynamic section ducks the band's boost
  (or deepens its cut) when that band gets loud.
- **Spectrum analyser** + live gain-reduction markers behind the EQ curve.
- **Zero latency**, no oversampling, one biquad per band → very light CPU.

## DSP design

```
out = dry + Σ_k  component_k · ((Gk · dynGain_k) − 1) · mix_k
```

Each band extracts its region with a single biquad (band-pass for bells,
low/high-pass for shelves) from the *dry* input, runs a one-pole envelope
detector on that component, computes a downward-compressed gain, and adds the
weighted component back. One biquad + one envelope per band, no look-ahead.

The DSP core in [`Source/dsp/`](Source/dsp/) is pure C++17 with **no JUCE
dependency**, so it can be unit-tested standalone.

## Building locally

Requires CMake ≥ 3.22 and a C++17 toolchain. JUCE is fetched automatically.

```sh
git clone <this repo>
cd dynamic-eq
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The bundle appears at `build/DynEQ_artefacts/Release/VST3/DynEQ.vst3`.

- **Linux**: install X11/GL/freetype/curl/alsa dev packages first (see the CI
  workflow for the exact `apt-get` list).
- **macOS**: just build with Xcode CLT; `clang` is already there.
- **Windows**: `cmake -B build -G "Visual Studio 17 2022" -A x64`.

> macOS note: the CI artifact is **unsigned**. On first load macOS Gatekeeper
> may quarantine it — right-click → Open, or move it into
> `~/Library/Audio/Plug-Ins/VST3/` and the DAW will scan it. For distribution,
> sign + notarize it (Apple Developer ID).

## Install (no installer)

Copy the bundle to the VST3 folder and rescan in your DAW:

| OS | Location |
|----|----------|
| Linux | `~/.vst3/` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` |
| Windows | `C:\Program Files\Common Files\VST3\` |

## DSP tests

The DSP core has standalone regression tests (gain accuracy, dynamics
behaviour, stability fuzz, CPU benchmark):

```sh
c++ -std=c++17 -O2 -I Source tests/dsp_tests.cpp -o dsp_tests
./dsp_tests
```
