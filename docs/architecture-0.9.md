# default_distortion 0.9 signal and control contract

## Signal flow

```text
raw input ───────────────► stereo-linked peak detector ─► Dynamic Drive offset
    │
    ▼
Input HP (3rd-order Butterworth, 18 dB/oct)
    │
    ├──────────────► latency-aligned contextual dry
    │
    ▼
tone pre ─► saturation/oversampling ─► tone post ─► M/S or T/S placement
    │                                                │
    │                             Smart Auto Gain observation
    │                                                │
    └──────────────────────── contextual Mix ◄─ Auto Gain on wet path
                                      │
                                      ▼
Output LP (3rd-order Butterworth, 18 dB/oct)
                                      │
                                      ▼
global Output ─► global bypass/latency guard ─► output
```

The detector always receives the complete raw plug-in input before Input HP,
routing, or multiband splitting. Unlinked bands keep independent envelope
state while receiving the same detector signal. Smart Auto Gain retains its
0.8 measurement lifecycle and observes the latency-aligned dry and routed wet
signals before makeup, Mix, Output LP, and global Output.

## New parameter contract

| Control | Range | Neutral/default | Processing rule |
| --- | --- | --- | --- |
| Route | M/S, T/S | M/S | Selects the placement domain |
| Placement | -100…+100% | 0% | Whole signal at 0; Mid/Transient at -100; Side/Sustain at +100 |
| Dynamic | -100…+100% | 0% | `clamp(base Drive + envelope × Dynamic × 36 dB, 0, 36 dB)` |
| Speed | 0…100% | 50% | Controls detector attack/release |
| Input HP | OFF/0…200 Hz | OFF | Pre-saturation 18 dB/oct Butterworth high-pass |
| Output LP | 2 kHz…20 kHz/OFF | OFF | Post-Mix 18 dB/oct Butterworth low-pass |

The filter cutoff coefficients use logarithmic parameter ranges, 25 ms control
smoothing, 10 ms wet/bypass crossfades, and a `0.45 × sample rate` Nyquist
guard. `OFF` is a true bypass after its transition completes.

## Speed curve

Attack and release are piecewise logarithmic interpolations copied from the
`default_eq` 0.5.3 contract:

| Speed | Attack | Release |
| --- | ---: | ---: |
| 0% | 100 ms | 1000 ms |
| 50% | 10 ms | 100 ms |
| 100% | 0.1 ms | 15 ms |

The follower uses sample-rate-derived exponential coefficients and a
stereo-linked peak (`max(abs(channel))`) per input sample. Dynamic parameter
movement is smoothed independently and never writes into the base Drive
parameter.

## RTA contract

The analyzer uses the fixed 8192-point `default_eq` medium-resolution profile:
2048-sample publish hop, Hann window, 1/24-octave linear-power smoothing,
65 ms display averaging, 1.5 dB decay, +4.5 dB/octave tilt, and a -80…0 dB
display range. Input and output use independent preallocated three-slot SPSC
swap chains; when the UI falls behind, stale frames are replaced without
blocking or allocating on the audio thread.
