# default_distortion 0.9 signal and control contract

## Signal flow

```text
raw input ─┬─► Input HP [D off] ─► contextual audio path
           └─► Input HP [D on]  ─► Dynamic Drive detector ─► Drive offset ─┐
                                                                          │
contextual audio path                                                     │
    │
    ├──────────────► latency-aligned contextual dry
    │
    ▼
tone pre ─► saturation/oversampling ◄─────────────────────────────────────┘
                          │
                          └─► tone post ─► M/S or T/S placement
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

With the Input HP `D` button off, the filter processes the main audio signal and
the Dynamic detector receives the unfiltered source. With `D` on, the main audio
remains unfiltered and the same HP processes only the detector signal. Linked
Multiband uses the shared full-range detector; unlinked Multiband applies each
band's HP route to that band's split-signal detector. Smart Auto Gain retains
its 0.8 measurement lifecycle and observes the latency-aligned dry and routed
wet signals before makeup, Mix, Output LP, and global Output.

## New parameter contract

| Control | Range | Neutral/default | Processing rule |
| --- | --- | --- | --- |
| Route | M/S, T/S | M/S | Selects the placement domain |
| Placement | -100…+100% | 0% | Whole signal at 0; Mid/Transient at -100; Side/Sustain at +100 |
| Dynamic | -100…+100% | 0% | `clamp(base Drive + envelope × Dynamic × 36 dB, 0, 36 dB)` |
| Speed | 0…100% | 100% | Controls detector attack/release |
| Input HP | OFF/0…2 kHz | OFF | 18 dB/oct Butterworth HP, routed to audio or detector |
| Input HP D | Audio, Detector | Audio | Chooses whether Input HP processes the main signal or only Dynamic's detector |
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
