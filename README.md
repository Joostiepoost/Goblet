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
| 1 | One deck: source strip, fragment, loop, reverse, crossfade, quality presets, transport; measurements | done |
| 2 | Three decks and the sum: meters, clip lamp, Play per deck and Play all, Mute, Solo; audio on/off | done |
| 3 | Plugin slots: two before and two after each fader, two on the sum, plugin windows, latency compensation | testing |
| 4 | Faders and MIDI: dB law, mapping window, smoothing, pickup, reversed ranges, 14-bit switch | |
| 5 | Live capture: sum and stems, tail, captures panel, snapshots | |
| 6 | Gesture re-render: replay a take's CCs offline at Render quality | |

The full design is in the Goblet project proposal.

## Using step 3: plugin slots

- **Fourteen slots**, all for your own AU and VST3 plugins:
  - two **before the fader** on each deck, in the row under the deck's waveform;
  - two **after the fader** for each deck, under its meter in the mixer column (for a limiter, a maximizer, or anything else that belongs after the fader);
  - two on the **sum**, at the foot of the mixer column, for a final limiter.
  The signal runs: deck, slot 1, slot 2, (fader, step 4), after-fader slot 1, slot 2, the sum, sum slot 1, slot 2, output.
- **Plugins...** scans for plugins. The first time, Goblet takes Effet's plugin list (or Trajet's), so no scan is needed.
- **Click an empty slot** to choose a plugin. **Click a filled slot** to open the plugin's window. **Right-click** (or the small arrow) for the menu: open window, **Bypass**, replace with, move to the other slot, remove.
- **Bypass** crossfades the plugin out over 20 ms; the plugin keeps running, so the timing doesn't jump and nothing clicks. A bypassed slot shows its name struck through and dimmed.
- **Latency compensation**: when one deck's plugins add more latency than another's, Goblet delays the faster decks so all three stay sample-aligned in the sum. The status line shows the plugin latency and how much each deck is delayed; a slot's tooltip shows its plugin's latency.
- Everything is remembered between sessions: which plugin sits in which slot, its settings and its bypass. Plugin settings are saved when you close its window, when you change a slot, and when you quit.

**Worth testing**:
- Load your usual plugins in different slots, including FabFilter (the AU sidechain fix from Effet is in), GRM, and a limiter or maximizer after a fader.
- Quit and reopen: every plugin comes back with its settings and bypass.
- Same file and fragment in two decks, a plugin with lookahead (a limiter, a linear-phase EQ) before one deck's fader: Play all should still sound as one deck, louder, without flanging. That is the latency compensation at work.
- Bypass on and off while playing: no clicks, no jump in time.
- Audio off and on again with plugins loaded, and a change of sample rate in Audio...: the plugins keep working.

## Using step 2: three decks, meters, mute and solo, audio on/off

- **Decks B and C** work exactly like deck A (see step 1 below). Keys: **1**, **2**, **3** play or stop a deck.
- **Play all** (or **Space**) starts every deck that has a file in the same sample.
- **M** mutes a deck (**Shift-1/2/3**), **S** solos it (**Option-1/2/3**). With any deck soloed, only soloed decks are heard; mute wins over solo. Both ramp over 5 ms, so they never click. A deck silenced by mute or by another deck's solo shows its letter dimmed.
- **Mixer column**: a stereo peak meter per deck, with a peak-hold line (1.5 s) and the highest recent peak in dB below it. The deck meters read after the deck's plugins and *before* mute and solo, so a muted deck still shows that it is playing and how loud it will be when it comes in.
- **Sum meter** (top right): what you hear. The red lamp lights when the sum reaches 0 dBFS and stays lit until you click it.
- **Audio on / Audio off** (**Shift-Cmd-A**): off closes the audio device, so Goblet uses no CPU while it sits in the background. The files and settings stay loaded. Play, Play all or Space turn audio back on.

**Worth testing**:
- Load the same file in two decks with the same fragment and press Play all: they should sound as one, louder (in phase), not as a flanged double.
- Three decks with fragments of different lengths, looping: they drift against each other, without clicks.
- Mute and solo while playing, quickly and repeatedly: no clicks.
- Audio off, then check Activity Monitor: Goblet should be near 0% CPU. Then Space: audio comes back and the decks start.
- Push the sum over 0 dBFS (three loud files): the lamp lights; click it to reset.

## Using step 1

The window already has its final layout: the decks on the left, the mixer on the right, and a dashed placeholder where the captures will go.

- **Load a file** into a deck: *Open...*, or drop a WAV or AIFF file on the deck.
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
5. Play all: decks started together stay sample-aligned. Mute and solo: who is heard in each combination, and no clicks from the ramps. Plugin latency: with a test plugin (10 ms latency) on one deck, both decks still arrive together; bypass switched while playing doesn't click or shift the timing.
6. CPU per deck, per preset, at the rate ratios that occur in practice.

Please commit `report.md` with GitHub Desktop so I can read your M4's CPU table. `report-cloud-test-machine.md` holds the same report from the slower machine the code was written on: there all checks pass, and the costliest realistic case (a 96 kHz file on a 44.1 kHz device at Render) takes about 35% of one core per deck. The M4 should need roughly two thirds of that. In the reverse-switch column, equal power and Bezier show 1.32 and 1.28 rather than 1.00: that is the expected level bump of those shapes on correlated material during the crossfade, not a click.
