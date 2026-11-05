# Goblet

A small live mixer: three WAV fragments, looped, each through its own plugins and a MIDI-controlled fader, captured as you play. Third app in the family after [Trajet](https://github.com/Joostiepoost/trajet) and [Effet](https://github.com/Joostiepoost/effet), and built mostly from Effet's code.

macOS, Apple Silicon only. Built with [JUCE](https://juce.com) 9 and CMake.

## Building

Requirements: Xcode (opened once, licence accepted), Homebrew, CMake (`brew install cmake`), the same as for Effet.

In Terminal, from this folder:

```
chmod +x build.sh   # only the first time
./build.sh
```

The first build downloads JUCE and takes a few minutes; later builds are much faster. The app opens when the build finishes, and lives at `build/Goblet_artefacts/Release/Goblet.app`.

To skip the JUCE download, point the build at the copy Effet already downloaded:

```
cmake -B build -DGOBLET_JUCE_PATH=../Effet/build/_deps/juce-src
./build.sh
```

(Adjust the path to where your Effet folder is. Only needed once; the setting is remembered.)

Other options:

- `./build.sh debug` — Debug build (slower, more checks)
- `./build.sh xcode` — generate and open an Xcode project, for debugging
- `./build.sh tests` — build and run the measurement program (see *Measurements* below)

## Status

| Step | Content | State |
| --- | --- | --- |
| 1 | One deck: source strip, fragment, loop, reverse, crossfade, quality presets, transport; measurements | testing |
| 2 | Three decks and the sum: meters, clip lamp, Play per deck and Play all, Mute, Solo | |
| 3 | Plugin slots: two before and two after each fader, two on the sum, plugin windows, latency compensation | |
| 4 | Faders and MIDI: dB law, mapping window, smoothing, pickup, reversed ranges, 14-bit switch | |
| 5 | Live capture: sum and stems, tail, captures panel, snapshots | |
| 6 | Gesture re-render: replay a take's CCs offline at Render quality | |

The full design is in the Goblet project proposal.

## Using step 1

The window already has its final layout: deck A at the top left, and dashed placeholders where decks B and C, the mixer and the captures will go.

- **Load a file** into deck A: *Open...*, or drop a WAV or AIFF file on the deck.
- **The source strip** shows the whole file. Drag the in or out flag to move it, or drag anywhere else to mark a new fragment. The dashed line is where the deck is reading.
- **In / Out**: double-click to type (`1:23.456`, `83.456` or `83`).
- **Play** (or key **1**) starts the deck at the beginning of its fragment; **Stop** fades it out over 3 ms and returns it to the start. A 3 ms fade-in at the start means a fragment that begins mid-waveform doesn't click.
- **Loop** on: the fragment repeats, with the seam crossfade. Off: it plays once and fades out over the last 3 ms before the out point.
- **Rev** plays the fragment backwards; switching while playing crossfades between the two directions. Reversed, a deck starts at its out point.
- **Crossfade**: length (0–200 ms) and shape (Equal power, Linear, S-curve, Bezier with its tension), used at the loop seam, when Play restarts a deck that is already playing, and when switching Rev.
- **Play all** (or **Space**) starts every deck that has a file in the same sample; Space again, or **■**, stops them all.
- **Quality**: the sinc presets from Effet (High 192, Very high 368, Max 512, Render 768 taps). Goblet never changes speed, so the sinc player works only when a file's rate differs from the device's. At equal rates the samples pass through unchanged and the preset makes no difference. The status line says which case each deck is in, with the read ratio and the taps.
- **Audio...**: device, sample rate and buffer size. CPU load is at the top right.

Everything is remembered between sessions: the file, fragment, loop, reverse, crossfade, quality and audio device.

**Worth testing**:
- A 44.1 kHz file on a 96 kHz device and a 96 kHz file on a 44.1 kHz device: listen to the presets, and watch the CPU at Render.
- A short fragment with Loop on, at different crossfade lengths and shapes.
- Rev switched while playing, with a short fragment: the playhead must stay inside the fragment (the bug found in Effet's first test).
- Play and Stop in quick succession, on a fragment that starts and ends mid-waveform: no clicks.
- Loop off: the deck stops at the out point.

## Measurements

`./build.sh tests` (about a minute) checks the deck player and writes `Measurements/report.md`:

1. Passthrough: at equal rates the deck outputs the file's samples unchanged.
2. The playhead stays inside the fragment, with reverse switched while playing.
3. Loop seam and reverse switch: no clicks with a crossfade.
4. Start, stop and play once: no clicks from the 3 ms ramps.
5. Play all: decks started together stay sample-aligned.
6. CPU per deck, per preset, at the rate ratios that occur in practice.

Please commit `report.md` with GitHub Desktop so I can read your M4's CPU table. `report-cloud-test-machine.md` holds the same report from the slower machine the code was written on: there all checks pass, and the costliest realistic case (a 96 kHz file on a 44.1 kHz device at Render) takes about 35% of one core per deck. The M4 should need roughly two thirds of that. In the reverse-switch column, equal power and Bezier show 1.32 and 1.28 rather than 1.00: that is the expected level bump of those shapes on correlated material during the crossfade, not a click.
