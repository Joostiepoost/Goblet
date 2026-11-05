# Goblet deck player: measurements (step 1)

Machine: Intel(R) Xeon(R) Processor @ 2.10GHz, Linux. Date: 5 Nov 2026 11:29:35am.

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

## CPU

Time for one deck to produce 1 s of stereo output, as a share of one core of this machine (white noise, looped). Three decks cost about three times as much; all three run on the same audio thread.

| Preset | 48.0 on 48.0 kHz (×1.000) | 44.1 on 48.0 kHz (×0.919) | 48.0 on 44.1 kHz (×1.088) | 44.1 on 96.0 kHz (×0.459) | 48.0 on 96.0 kHz (×0.500) | 96.0 on 48.0 kHz (×2.000) | 96.0 on 44.1 kHz (×2.177) | 192.0 on 48.0 kHz (×4.000) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| High | 0.0% | 1.4% | 4.1% | 2.8% | 2.7% | 8.3% | 8.2% | 18.2% |
| Very high | 0.1% | 3.1% | 8.2% | 5.5% | 5.0% | 15.8% | 15.9% | 31.1% |
| Max | 0.0% | 3.5% | 11.1% | 7.1% | 6.8% | 21.7% | 23.5% | 45.2% |
| Render | 0.0% | 5.1% | 16.1% | 11.2% | 11.5% | 36.6% | 34.6% | 67.7% |

