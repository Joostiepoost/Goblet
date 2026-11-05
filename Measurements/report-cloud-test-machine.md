# Goblet deck player: measurements (step 1)

Machine: Intel(R) Xeon(R) Processor @ 2.10GHz, Linux. Date: 5 Nov 2026 3:52:57pm.

## Passthrough at equal rates

1.5 s of white noise, file and device at 96 kHz, Max: 960 of 143712 samples differ from the file after the 3 ms start ramp.

## Playhead stays inside the fragment

A 252 ms fragment of a 44.1 kHz file on a 48 kHz device, 200 ms crossfade (limited to half the fragment), reverse switched on after 0.5 s and off after 3 s, 5 s of play.

Playhead min / max: 0.4480 / 0.6999 s; fragment 0.448 - 0.700 s: inside.

## Loop seam and reverse switch

A 1 kHz sine at 48 kHz (amplitude 0.5), fragment 123.4 ms (not a whole number of periods), 2 s of play. **Step**: the largest sample-to-sample step, relative to the steepest step of the sine: 1.00 means no click.

| Crossfade | Loop seam: step | Reverse switch: step |
| --- | --- | --- |
| none (0 ms) | 5.41 | 1.32 |
| 10 ms equal power | 1.00 | 1.32 |
| 10 ms linear | 1.00 | 1.00 |
| 10 ms S-curve | 1.00 | 1.00 |
| 10 ms Bezier 0.5 | 1.00 | 1.28 |
| whole file as fragment, 10 ms equal power: forward / reversed | 1.00 / 1.32 |

## Start, stop and play once

The same sine, fragment starting at a peak of the waveform. Step relative to the sine's own steepest step; without the 3 ms ramps a start at a peak would jump by the full amplitude (about 7.64489).

| Case | Step | Note |
| --- | --- | --- |
| Start at a peak, stop after 0.2 s | 1.00 | silent after the stop ramp |
| Loop off, 100 ms fragment | 1.00 | last sound at 100.15 ms (out point at 100.15 ms); stopped |

## Play all

Two decks with the same 44.1 kHz file on a 48 kHz device, started with Play all: largest difference from twice one deck 0.000000000 (sample-aligned).

## Mute and solo

Decks A and B play the same 1 kHz sine (level of one deck: 0.177 RMS). Level heard in each phase:

| Phase | Expected | Measured RMS |
| --- | --- | --- |
| A and B | two decks | 0.354 |
| B muted | one deck | 0.177 |
| B muted, A soloed | one deck | 0.177 |
| A soloed and muted, B muted | silence | 0.000000 |
| all cleared | two decks | 0.354 |

Largest sample-to-sample step over the whole run, relative to the steepest step of the two-deck sine: 1.00 (no clicks).

## Plugin latency and bypass

Decks A and B play the same noise; A has a test plugin with 10 ms latency before its fader, B none. Goblet reports a deck latency of 480 samples for A and delays B by 480 samples.

- Largest difference from twice the file delayed by 10 ms: 0.000000000 (aligned).
- Largest level in the first 10 ms (should be silence): 0.000000000.
- Bypass switched while playing (a 1 kHz sine through a test plugin that halves the level, with 10 ms latency): level 0.250 with the plugin, 0.500 bypassed, 0.250 back on; largest step 1.00 (no clicks: the dry path is delayed by the plugin's latency, so the timing does not jump).

## Trim, faders and MIDI

The fader law (position 0..1, as a 7-bit controller sends it):

| CC | Position | Level |
| --- | --- | --- |
| 127 | 1.000 | 12.0 dB |
| 114 | 0.898 | 5.9 dB |
| 102 | 0.803 | 0.2 dB |
| 89 | 0.701 | -4.0 dB |
| 64 | 0.504 | -11.8 dB |
| 32 | 0.252 | -29.9 dB |
| 13 | 0.102 | -49.7 dB |
| 3 | 0.024 | -69.1 dB |
| 0 | 0.000 | silence |

- Trim -6 dB with the fader at +12 dB: measured 6.00 dB (expected +6.00).
- A CC sweeping fader A from 127 to 0 over 1.8 s (7-bit, 20 ms smoothing; the 1 s file loops as a whole meanwhile): level 10.1 dB at the start, -14.7 dB halfway, silent at the end: yes. Largest bend in the output, relative to the sine's own at that level: 1.06 (smooth: no zipper steps).
- 14-bit (CC 7 + CC 39 = 101, 77): fader B at position 0.79381 (expected 0.79381, -0.25 dB).
- One CC on faders A and B, B reversed (a crossfade): CC 0: A silence dB, B 12.0 dB; CC 64: A -11.8 dB, B -12.3 dB; CC 127: A 12.0 dB, B silence dB.
- Pickup, fader C at 0.800 after a mouse move, controller moving 20, 60, 110, 90, 40 (positions as CC/127): 20 -> 0.800 (waiting); 60 -> 0.800 (waiting); 110 -> 0.866; 90 -> 0.709; 40 -> 0.315. It ignores the controller until it passes 0.8, then follows.

## Capture: sum and stems

Deck A plays noise through a test plugin with 10 ms latency, fader at -6 dB; deck B plays a sine once, trim +3 dB; the sum has a test plugin with 5 ms latency. Rec, then both decks stop after 1.6 s.

- Files: sum 91696 samples, stem A 91936, stem B 91936.
- Largest difference between the sum and stem A + stem B: 0.000000030 (peak level 0.779): the stems add up to the sum, sample for sample.
- The tail: the decks stopped at block 300, the capture ended at block 360 (320 ms later: the plugins' delay, the stop ramp and 0.3 s of silence).

## Re-render from the gesture

A 3.2 s take: three decks (one 44.1 kHz file on the 48 kHz device), test plugins with latency on deck A, after fader B and on the sum. The decks were already playing when Rec was pressed. During the take: a CC moving fader B, a CC moving the gain of deck A's plugin (40 ms smoothing), a mouse move on fader A, mute on C, reverse on B, A stopped and started again, a trim change, a bypass and a solo; then Rec again (cut). The gesture holds 845 events (187 fader, 600 plugin parameter, 2 start/stop) over 3.20 s.

| Re-render | Stream | Same length | Largest difference | Peak |
| --- | --- | --- | --- | --- |
| Max (as live) | sum | yes | 0.000000000 | 1.283 |
| Max (as live) | stem A | yes | 0.000000000 | 0.809 |
| Max (as live) | stem B | yes | 0.000000000 | 0.558 |
| Max (as live) | stem C | yes | 0.000000000 | 0.229 |
| Render | sum | yes | 0.056259394 | 1.283 |
| Render | stem A | yes | 0.056259401 | 0.809 |
| Render | stem B | yes | 0.000000030 | 0.558 |
| Render | stem C | yes | 0.000021622 | 0.229 |

- The Re-render button's path (snapshot and gesture read from disk, a new take written): done. Goblet test take render.wav: 153600 samples; Goblet test take render A.wav: 153600 samples; Goblet test take render B.wav: 153600 samples; Goblet test take render C.wav: none; the snapshot was copied.

At the live take's quality the re-render is identical to it, sum and stems, even though the decks were already playing when Rec was pressed (the gesture keeps where each deck, crossfade and gain ramp was at that moment). At Render quality only what goes through the sinc player differs: deck A's 44.1 kHz file, and the crossfades of the others. With real plugins the first moments can still differ if the decks were already playing: a reverb in the second mixer starts without the tail it had in the live one.

## CPU

Time for one deck to produce 1 s of stereo output, as a share of one core of this machine (white noise, looped). Three decks cost about three times as much; all three run on the same audio thread.

| Preset | 48.0 on 48.0 kHz (×1.000) | 44.1 on 48.0 kHz (×0.919) | 48.0 on 44.1 kHz (×1.088) | 44.1 on 96.0 kHz (×0.459) | 48.0 on 96.0 kHz (×0.500) | 96.0 on 48.0 kHz (×2.000) | 96.0 on 44.1 kHz (×2.177) | 192.0 on 48.0 kHz (×4.000) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| High | 0.1% | 2.2% | 6.0% | 3.0% | 2.6% | 8.0% | 8.0% | 16.5% |
| Very high | 0.0% | 2.5% | 7.9% | 5.0% | 4.7% | 15.2% | 16.9% | 31.9% |
| Max | 0.0% | 3.5% | 10.8% | 7.6% | 6.7% | 21.6% | 21.7% | 42.6% |
| Render | 0.0% | 5.8% | 16.3% | 10.6% | 9.8% | 32.5% | 32.3% | 66.3% |

