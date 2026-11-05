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
| 3 | Plugin slots: two before and two after each fader, two on the sum, plugin windows, latency compensation | done |
| 4 | Trims and faders (+12 dB headroom), MIDI: mapping window, smoothing, Jump/Pickup, reversed ranges, 14-bit switch | done |
| 5 | Live capture: sum and stems, tail, captures panel, snapshots | done |
| 6 | Gesture re-render: replay a take offline at Render quality | testing |

The full design is in the Goblet project proposal.

## Using step 6: re-render

- **Every take now also records its gesture** (`<take>.gesture`, beside the snapshot): everything that changed the mix during the take, with its exact time: faders, trims, mute, solo, deck starts and stops, loop, reverse, fragments, crossfades, bypass, and the plugin parameters your MIDI mappings moved. It also keeps where each deck, crossfade and gain ramp was at the moment you pressed Rec.
- **Re-render** (captures panel) plays the take again offline, faster than real time, at **Render** quality: from the take's snapshot it builds a second mixer with the same files, fragments and plugins (fresh instances, with the settings they had at Rec), and replays the gesture into it. The live mix keeps running meanwhile; the panel shows the progress, and clicking the button again cancels.
- The result is a new take beside the original, `<take> render`, with its own stems and a copy of the snapshot, as long as the original.
- At the quality the take was played at, the re-render is identical to the live take (the test program checks this sample for sample). At Render quality, what differs is what goes through the sinc player: files at another rate than the device, and the crossfades.
- **What can differ** with real plugins:
  - plugins that don't behave the same on every run (random modulation, free-running LFOs, anything that depends on the clock);
  - knobs you turned in a plugin's own window during the take (the plugin keeps those to itself; MIDI-mapped parameters are recorded);
  - if the decks were already playing when you pressed Rec: the plugins in the second mixer start from silence, so a reverb has no tail from before the take during the first moments. Starting a take with Rec from a stop (which starts the decks) avoids this.
- Plugins render with their offline flag set; some (convolution reverbs, some analog models) then use higher quality modes.

**Worth testing**:
- A take with Touché on the faders and a mapped plugin parameter, started from a stop; Re-render it, drag both into Reaper and compare (polarity-invert one: what is left is only the Render-quality difference).
- A take with plugins you suspect of randomness, to see how close the re-render comes.
- Cancel a running re-render, and re-render while playing live.

Note on loops: when a fragment is a whole file whose end flows on into its start (a seamless loop), the Equal power crossfade raises the level by up to 3 dB at the loop point; Linear or S-curve keep it flat there.

## Using step 5: live capture

- **Rec** (or **R**) starts a take. If no deck is playing, it starts them all (as Play all). A take is:
  - `Goblet <date> <time>.wav`: the **sum**, what you hear, after the sum's slots;
  - `<take> stems/<take> A.wav`, `B.wav`, `C.wav`: one **stem** per deck that has a file, taken just before the sum (after the deck's plugins, fader, mute and solo), so the stems add up exactly to the sum before the sum's slots;
  - `<take>.goblet`: the **snapshot**, the whole session at the moment you pressed Rec (files, fragments, plugins with their settings, trims, faders, mute/solo, MIDI mappings, quality).
  All files are 32-bit float WAV at the device's rate, in **Music/Goblet Captures**.
- **Alignment**: each file skips its own latency at the start, so in Reaper the sum and the stems start on the same sample, whatever plugins you use.
- **The tail**: when the last deck stops, the capture runs on until the sound has died away (below -90 dBFS for 0.3 s, at most 30 s), so reverb tails are kept. If you start a deck again in the meantime, the take simply goes on. **Rec** again ends the take at once. While recording, the button shows the take's running time.
- **Captures panel** (bottom of the window), newest first:
  - double-click (or **Play**) to listen; it plays on top of whatever is running and is not captured itself;
  - the **triangle** unfolds a take to show its stems, which you can listen to and drag separately;
  - **drag** a take into Reaper or the Finder; **Shift-drag** a take to drag the sum and all its stems at once;
  - **Rename...** renames the take, its stems and its snapshot together; **Delete** moves all of them to the bin; **Show** reveals the file in the Finder; **Open folder** opens Goblet Captures;
  - **Restore snapshot...** brings back the session that produced the take (after asking, since it replaces the current one).

**Worth testing**:
- A take with all three decks and plugins with latency: drag it into Reaper with Shift, mute the sum and play the stems: they should sound exactly like the sum (minus what the sum's slots do).
- The tail: a long reverb on a deck, stop the decks, and check that the take keeps the whole tail.
- Restore snapshot after changing files, plugins and faders.
- Rename and Delete: the stems and the snapshot follow.

## Using step 4: trims, faders and MIDI

- **Trim** (each deck, left of its plugin slots): raises or lowers the deck by up to 24 dB *before* its plugins and fader, so files of very different loudness arrive at similar levels and the faders start from the same place. Drag up/down (Shift: finer), double-click the knob for 0 dB, or double-click the value to type one.
- **Faders** (mixer column, beside each meter): the dB law has **+12 dB at the top** and 0 dB at about four fifths of the travel (CC 102 on a 7-bit controller); -12 dB is halfway, -30 dB a quarter of the way up, and the bottom is silence. Double-click a fader for 0 dB, or double-click its value to type one. The scale on the left belongs to the faders; under each strip are the fader's level and the deck's recent peak (pk).
- **MIDI...** (**Shift-Cmd-M**) opens the mappings window, as in Effet:
  - the MIDI input at the top (all inputs, none, or one device);
  - **+ Mapping** adds a row; the first three start on faders A, B and C, on the next free CCs. Up to 16 mappings;
  - per row: channel, **CC** (chosen in its menu, never by moving a control), **14-bit** (CC 0-31 with its fine part on CC 32-63, for a controller that sends pairs; Touché sends 7-bit), the **target** (a fader, a trim, or any parameter of a loaded plugin), the controller range used (**In**), the target range (**Out**, faders and trims in dB, plugins in their own units; click to type or drag), the **curve**, the **smoothing** time (20 ms by default, so 7-bit steps aren't heard) and the **takeover**;
  - **Jump**: the target follows the controller as soon as it moves. **Pickup**: after you moved the target with the mouse, the controller takes over only once it passes the target's value (the row's bar turns orange while it waits);
  - **a crossfade from one CC**: map it to fader A, and again to fader B with the Out range reversed (start +12 dB, end silence);
  - the **Incoming** list shows every controller that sends, so you can see which CC numbers each Touché axis uses.
  - The MIDI button lights up when MIDI arrives. Mappings, trims and fader positions are remembered.
- **Whole-file loops** no longer dip at the loop point: when a fragment ends at the end of its file, there is nothing to read past the out point, so the seam crossfade now starts just before it. (Effet does the same as Goblet did before this fix: a fragment ending exactly at the end of the file dips at each loop by the crossfade length.)

**Worth testing**:
- Touché on the three faders: slow fades should be smooth, without steps.
- A crossfade from one Touché axis between two decks (one mapping reversed).
- Pickup: move a fader with the mouse, then the controller: the fader stays put until the controller passes it.
- A mapping on a plugin parameter (a filter cutoff, a reverb mix) with its Out range narrowed, typed in the plugin's units.
- Trims: two files of very different loudness, trimmed so that both sound about equally loud with the faders at 0 dB.
- Loop a whole file: no dip at the loop point.

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

- **Decks B and C** work exactly like deck A (see step 1 below). Keys: **A**, **B**, **C** play or stop a deck.
- **Play all** (or **Space**) starts every deck that has a file in the same sample.
- **M** mutes a deck (**Shift-A/B/C**), **S** solos it (**Option-A/B/C**). With any deck soloed, only soloed decks are heard; mute wins over solo. Both ramp over 5 ms, so they never click. A deck silenced by mute or by another deck's solo shows its letter dimmed.
- **Mixer column**: a fader (step 4) and a stereo peak meter per deck, with a peak-hold line (1.5 s) and the highest recent peak in dB below it. The deck meters read after the deck's plugins and *before* mute and solo, so a muted deck still shows that it is playing and how loud it will be when it comes in.
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
- **Play** (or key **A**, **B**, **C**) starts the deck at the beginning of its fragment; **Stop** fades it out over 3 ms and returns it to the start. A 3 ms fade-in at the start means a fragment that begins mid-waveform doesn't click.
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
5. Play all: decks started together stay sample-aligned. Mute and solo: who is heard in each combination, and no clicks from the ramps. Plugin latency: with a test plugin (10 ms latency) on one deck, both decks still arrive together; bypass switched while playing doesn't click or shift the timing. Trim, faders and MIDI: the fader law, trim and fader levels, a slow 7-bit sweep without zipper steps, 14-bit, a crossfade from one CC, and Pickup. Capture: with plugin latencies on a deck and on the sum, the stems add up to the sum sample for sample; the tail ends the take after the decks stop. Re-render: a take full of MIDI, mouse moves, mute, reverse and stop/start, replayed from its gesture, is identical to the live take at the same quality; and the Re-render button's whole path (files on disk, the new take) works.
6. CPU per deck, per preset, at the rate ratios that occur in practice.

Please commit `report.md` with GitHub Desktop so I can read your M4's CPU table. `report-cloud-test-machine.md` holds the same report from the slower machine the code was written on: there all checks pass, and the costliest realistic case (a 96 kHz file on a 44.1 kHz device at Render) takes about 35% of one core per deck. The M4 should need roughly two thirds of that. In the reverse-switch column, equal power and Bezier show 1.32 and 1.28 rather than 1.00: that is the expected level bump of those shapes on correlated material during the crossfade, not a click.
