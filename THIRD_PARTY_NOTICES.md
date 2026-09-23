# Third-party notices

## Vital Soft Clip and Hard Clip

`Source/DistortionEngine.cpp` contains scalar copies/adaptations of Vital's
Soft Clip, Hard Clip, and rational `futils::tanh` implementation:

- Vital: <https://github.com/mtytel/vital>
- commit: `636ca0ef517a4db087a6a08a6a8a5e704e21f836`
- `src/synthesis/effects/distortion.cpp`
- `src/interface/editor_sections/distortion_section.cpp`
- `src/synthesis/framework/futils.h`

Changes in default_distortion include conversion from Vital's SIMD types to
scalar `float`, integration with the plug-in's serial stage path, and use of
Character to morph Soft Clip from Vital soft clipping to a bounded cubic
transfer, and Hard Clip from Vital hard clipping to Vital soft clipping.
Character 0 retains the copied Vital transfer. The Hard Clip preview also uses
Vital's `-1..+1` transfer domain, direct dB-drive mapping, and 90% draw scale.

Copyright (C) 2013-2019 Matt Tytel.
Licensed under GNU GPL version 3 or later. See `LICENSES/GPL-3.0.txt`.

## CHOW Tape Model and BYOD hysteresis

`Source/ChowTapeHysteresis.h` is a scalar adaptation of the Jiles-Atherton
hysteresis implementation developed by Jatin Chowdhury and contributors:

- CHOW Tape Model / AnalogTapeModel:
  <https://github.com/jatinchowdhury18/AnalogTapeModel>
- BYOD:
  <https://github.com/Chowdhury-DSP/BYOD>

The adaptation was prepared from:

- AnalogTapeModel commit
  `604372e4ffd9690c3e283362e4598cb43edbb475`
- BYOD commit
  `1cf22b6ac802b9dc33cfc9f8dd6af5b3c3e40bc9`
- `Plugin/Source/Processors/Hysteresis/HysteresisOps.h`
- `Plugin/Source/Processors/Hysteresis/HysteresisProcessing.*`
- `src/processors/drive/hysteresis/HysteresisOps.h`
- `src/processors/drive/hysteresis/HysteresisProcessing.*`

Changes in default_distortion include removal of SIMD and chowdsp utility
dependencies, scalar per-channel/per-stage state, defensive finite/denominator
checks, and adaptation from a three-control standalone processor to the
plug-in's Drive and Character controls.

Copyright (C) Jatin Chowdhury and CHOW/BYOD contributors.
Licensed under GNU GPL version 3. See `LICENSES/GPL-3.0.txt`.

## ZLSplitter transient/sustain separation

`Source/TransientSplitter.h` adapts the transient/steady separation from
ZLSplitter:

- repository: <https://github.com/ZL-Audio/ZLSplitter>
- revision: `2f50824ab925eeff7950986eac640dab43c3ce67`
- upstream boundary: the transient/steady splitter, 75% overlap, Hann windows,
  5x5 time/frequency median masks, complementary output, and parameter transforms
- local changes: KFR was replaced by JUCE FFT; storage is preallocated in
  `prepare`; the class was reduced to the fixed routing contract used by
  `default_distortion`
- copyright: Copyright (C) 2026 zsliu98 and ZL-Audio contributors
- license: GNU Affero General Public License version 3 only

The adaptation was first audited in `default_eq` 0.5.3 at revision
`d242ffab803a54f60ebabad31a6b2cf2d6dd408e`. The complete license is included
in `LICENSES/AGPL-3.0.txt`.

## JetBrains Mono

The interface uses an embedded copy of JetBrains Mono so that glyph shapes and
font metrics do not depend on fonts installed by the host operating system:

- repository: <https://github.com/JetBrains/JetBrainsMono>
- release: `v2.304`
- revision: `cd5227bd1f61dff3bbd6c814ceaf7ffd95e947d9`
- files: `Resources/Fonts/JetBrainsMono/JetBrainsMono-Medium.woff2`,
  `Resources/Fonts/JetBrainsMono/JetBrainsMono-ExtraBold.woff2`, and their
  matching TTF files
- copyright: Copyright 2020 The JetBrains Mono Project Authors
- license: SIL Open Font License 1.1

The WOFF2 files are used by the HTML interface prototypes. The matching static
TTFs are retained for embedding into the JUCE interface. The complete license is
included in `LICENSES/OFL-JetBrainsMono.txt`.

## JUCE

JUCE 8.0.15 is fetched at configure time and is not vendored in this
repository. This open-source build uses JUCE under AGPLv3:
<https://github.com/juce-framework/JUCE>.
