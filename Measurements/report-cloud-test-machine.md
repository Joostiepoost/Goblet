# Goblet deck player: measurements (step 1)

Machine: Intel(R) Xeon(R) Processor @ 2.10GHz, Linux. Date: 5 Nov 2026 12:05:58pm.

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

## CPU

Time for one deck to produce 1 s of stereo output, as a share of one core of this machine (white noise, looped). Three decks cost about three times as much; all three run on the same audio thread.

| Preset | 48.0 on 48.0 kHz (×1.000) | 44.1 on 48.0 kHz (×0.919) | 48.0 on 44.1 kHz (×1.088) | 44.1 on 96.0 kHz (×0.459) | 48.0 on 96.0 kHz (×0.500) | 96.0 on 48.0 kHz (×2.000) | 96.0 on 44.1 kHz (×2.177) | 192.0 on 48.0 kHz (×4.000) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| High | 0.0% | 1.4% | 4.1% | 2.7% | 2.6% | 8.0% | 8.0% | 16.4% |
| Very high | 0.0% | 2.5% | 9.5% | 8.2% | 6.4% | 16.8% | 16.4% | 33.5% |
| Max | 0.0% | 4.0% | 11.4% | 8.0% | 8.0% | 21.3% | 21.3% | 43.3% |
| Render | 0.0% | 7.5% | 16.3% | 11.0% | 9.7% | 31.9% | 32.1% | 64.0% |

