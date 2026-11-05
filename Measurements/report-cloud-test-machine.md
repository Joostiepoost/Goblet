# Goblet deck player: measurements (step 1)

Machine: Intel(R) Xeon(R) Processor @ 2.10GHz, Linux. Date: 5 Nov 2026 1:59:27pm.

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

Largest sample-to-sample step over the whole run, relative to the steepest step of the two-deck sine: 1.41 (CLICK).

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

## CPU

Time for one deck to produce 1 s of stereo output, as a share of one core of this machine (white noise, looped). Three decks cost about three times as much; all three run on the same audio thread.

| Preset | 48.0 on 48.0 kHz (×1.000) | 44.1 on 48.0 kHz (×0.919) | 48.0 on 44.1 kHz (×1.088) | 44.1 on 96.0 kHz (×0.459) | 48.0 on 96.0 kHz (×0.500) | 96.0 on 48.0 kHz (×2.000) | 96.0 on 44.1 kHz (×2.177) | 192.0 on 48.0 kHz (×4.000) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| High | 0.0% | 1.3% | 3.9% | 2.8% | 2.5% | 7.8% | 7.8% | 15.2% |
| Very high | 0.0% | 2.3% | 7.2% | 4.3% | 4.4% | 14.0% | 15.1% | 30.0% |
| Max | 0.0% | 3.2% | 9.3% | 6.5% | 6.2% | 19.3% | 19.6% | 46.0% |
| Render | 0.0% | 4.8% | 15.1% | 12.6% | 12.2% | 32.1% | 29.2% | 59.5% |

