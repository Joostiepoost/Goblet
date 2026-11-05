# Goblet deck player: measurements (step 1)

Machine: Intel(R) Xeon(R) Processor @ 2.10GHz, Linux. Date: 5 Nov 2026 12:37:32pm.

## Passthrough at equal rates

1.5 s of white noise, file and device at 96 kHz, Max: 0 of 143712 samples differ from the file after the 3 ms start ramp (bit-identical).

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

## CPU

Time for one deck to produce 1 s of stereo output, as a share of one core of this machine (white noise, looped). Three decks cost about three times as much; all three run on the same audio thread.

| Preset | 48.0 on 48.0 kHz (×1.000) | 44.1 on 48.0 kHz (×0.919) | 48.0 on 44.1 kHz (×1.088) | 44.1 on 96.0 kHz (×0.459) | 48.0 on 96.0 kHz (×0.500) | 96.0 on 48.0 kHz (×2.000) | 96.0 on 44.1 kHz (×2.177) | 192.0 on 48.0 kHz (×4.000) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| High | 0.0% | 1.4% | 4.1% | 2.8% | 2.6% | 7.9% | 8.1% | 19.0% |
| Very high | 0.0% | 2.6% | 8.7% | 6.2% | 5.3% | 20.0% | 15.4% | 30.2% |
| Max | 0.0% | 3.8% | 11.1% | 6.9% | 6.6% | 21.8% | 24.6% | 46.8% |
| Render | 0.0% | 5.2% | 16.5% | 10.6% | 11.3% | 32.9% | 32.5% | 67.2% |

