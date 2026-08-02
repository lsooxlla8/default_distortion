# default_distortion

![default_distortion plug-in interface](docs/default_distortion-clean.png)

~~I~~ ChatGPT made `default_distortion` because one day I had nothing to do
and wanted a free, convenient everyday saturator. I used as much AI as
possible and did nothing myself. It contains a bunch of the most default
saturation algorithms that have already appeared absolutely everywhere. We
even stole some of the code from other developers.*

The optional multiband panel splits the signal into two, three, or four bands.
Its draggable crossover lines support 6, 12, 24, 36, and 48 dB/oct slopes,
Minimum Phase and Linear Phase operation, linked or independent saturation
settings, per-band Solo/Bypass/Trim, and a simultaneous input/output RTA.

In linked mode every band uses the main saturation controls. Unlinking copies
the current controls into all bands; selecting a band in the RTA then exposes
its own Mode, Drive, Character, Secondary, Asymmetry, Tone, Stages, and Mix.
Linking again copies the selected band to all bands.

Regular Auto Gain uses the existing compensation independently in every band.
Smart Auto Gain instead measures the complete summed multiband result before
the final Output control.

\* In boring legal terms: copied or adapted code from open-source projects
under their GPL-compatible licences.

## Thanks and third-party code

- [Vital](https://github.com/mtytel/vital) by Matt Tytel and contributors —
  GPLv3 soft-clip and hard-clamp code.
- [CHOW Tape Model](https://github.com/jatinchowdhury18/AnalogTapeModel) and
  [BYOD](https://github.com/Chowdhury-DSP/BYOD) by Jatin Chowdhury and
  contributors — GPLv3 tape-hysteresis code.
- [JUCE](https://github.com/juce-framework/JUCE) — AGPLv3 plug-in framework.
- Exact revisions and licences:
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Algorithms

1. Soft Clip
2. Hard Clip
3. Diode Clipper
4. Triode Stage
5. Transistor / FET
6. Tape Hysteresis
7. Harmonic Morph
8. Phase Distortion
9. Spectral Clip
10. Sine Erosion
11. Sign / Square
12. Zero-Square
13. Full-Wave Rectifier
14. Soft Full-Wave
15. Transformer Core
16. Class-B Saturation
17. Topology Fold
18. Recursive Foldback
19. Sine Fold
20. Chebyshev Fold
21. Modulo Wrap
22. Downsample
23. Bit Crusher
24. Bit Rotation
25. Delta Crusher
26. Slew Limiter
27. Schmitt Hysteresis
28. Feedback Saturator
29. Resonant Feedback Clip
30. Dynamic Sag

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure
```

## Licence

default_distortion is open source under `AGPL-3.0-only`. The adapted CHOW/BYOD
hysteresis source remains `GPL-3.0-only` and is combined under GPLv3 section 13.
See `LICENSE.md`, `LICENSES/`, and `THIRD_PARTY_NOTICES.md`.
