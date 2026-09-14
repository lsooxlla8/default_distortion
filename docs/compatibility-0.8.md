# default_distortion 0.8 compatibility inventory

This inventory freezes the public contract immediately before the 0.9 schema
extension. `Tests/PluginContractTests.cpp` asserts the ordered manifest and
legacy-state migration; `Tests/DspTests.cpp` owns the deterministic audio and
all-30-visualization baselines.

## Host parameter order

The 0.8 manifest contains 67 parameters. Their IDs and order are immutable:

1. Master: `mode`, `drive`, `character`, `secondary`, `asym`, `asymStereo`,
   `tone`, `stages`, `mix`, `output`, `quality`, `autoGain`, `pluginEnabled`,
   `multibandEnabled`, `multibandLink`, `multibandBandCount`,
   `multibandPhase`.
2. Crossovers, interleaved by crossover: `crossover1Frequency`,
   `crossover1Slope` through `crossover3Frequency`, `crossover3Slope`.
3. Bands 1–4, in band-major order: `Mode`, `Drive`, `Character`, `Secondary`,
   `Asym`, `AsymStereo`, `Tone`, `Stages`, `Mix`, `Bypass`, `Trim`.

The 0.9 parameters are appended after this complete list. They never replace or
renumber a 0.8 parameter.

## Ranges and defaults

| Parameter family | Range | 0.8 default |
| --- | --- | --- |
| Mode | 30 choices | first algorithm |
| Drive | 0…36 dB | 0 dB |
| Character, Asym, Tone | -1…+1 | 0 |
| Secondary | 0…1 | 0 |
| Stereo Asymmetry | off/on | off |
| Stages | 1…8 | 1 |
| Mix | 0…1 | 1 |
| global Output | -24…+12 dB | 0 dB |
| Oversampling | OFF/2x/4x/8x | OFF |
| Auto Gain | Off/Regular/Smart | Regular |
| Multiband | off/on | off |
| Link | off/on | on |
| Band count | 2/3/4 | 4 |
| Phase | Minimum/Linear | Minimum |
| Crossover frequency | 20 Hz…20 kHz, logarithmic | 100/500/2000 Hz |
| Crossover slope | 6/12/24/36/48 dB/oct | 24 dB/oct |
| Band Bypass | off/on | off |
| Band Trim | -12…+12 dB | 0 dB |

Master and per-band saturation parameters retain their original formatting,
automation intervals, defaults, and contextual linked/unlinked ownership.

## State and processing

- The last 0.8 state schema is 5. Schema-5 loads initialize every appended 0.9
  parameter to its neutral value before replacing the APVTS state.
- Link off copies the current master saturation context to all four bands. Link
  on promotes the selected band to master and then copies it to all bands.
- The 30 algorithm transfer/preview arrays, deterministic Auto Gain tables,
  oversampling behavior, crossover topology, phase modes, and bypass latency
  remain the baseline. Neutral 0.9 processing is compared bit-for-bit with a
  clean 0.8 build for the deterministic regression fixtures.

## Editor interaction contract

- Full-cell algorithm, Auto Gain, OS, power, multiband, link, phase, solo, and
  bypass hit targets remain parameter-backed.
- Rotary/vertical controls retain vertical drag, Shift fine adjustment, wheel,
  editable value text, and double-click reset. Popup/right-click on resettable
  sliders follows the same reset path.
- Shift-drag in unlinked multiband mode retains one grouped host gesture per
  affected band; normal drag remains contextual to the selected band.
- Crossover drag preserves ordering constraints; right-click and double-click
  reset crossover/Trim; clicking the analyzer selects a band; slope badges open
  the slope selector.
- Existing algorithm stepping, theme toggle, contextual mode labels, band
  selection, S/B, and Smart Auto Gain progress behavior are retained.
