# TODO

## 0.9.0 — new interface, routing, Drive modulation, and tone filters

Version 0.9.0 migrates `default_distortion` to the approved alternative
`default_` interface and adds four explicitly approved processing features. This
is not an interface-only release: all existing 0.8.x processing and behavior are
preserved except where the new M/S–T/S routing, Dynamic/Speed modulation, Input HP,
and Output LP features deliberately extend the signal path.

### Sources of truth

- `docs/prototypes/default_distortion_alternative/index.html`
- `docs/prototypes/default_distortion_alternative/styles.css`
- `docs/prototypes/default_distortion_alternative/app.js`
- `docs/plugin-family-design-system.md`
- the current production `DistortionEngine::makeVisualization` output for all
  saturation visualizations;
- the production `default_eq` 0.5.3 analyzer/RTA at commit `d242ffab`, including
  its analyzer transport, constants, smoothing, drawing order, theme behavior,
  and update lifecycle;
- the routing implementation in `default_eq`, especially
  `Source/DSP/TransientSplitter.h`, `Source/ProcessorRouting.cpp`, and the
  placement branches in `Source/PluginProcessor.cpp`;
- upstream [ZLSplitter](https://github.com/ZL-Audio/ZLSplitter) revision
  `2f50824ab925eeff7950986eac640dab43c3ce67`, audited under AGPL-3.0.

The previous `docs/prototypes/default_distortion/` redesign is superseded. It
must not be used as the 0.9.0 layout reference.

### Final design contract

- Use the alternative prototype's `648 x 286` compact and `648 x 450` expanded
  reference frames, `640 px` inner width, `60 px` header, `182 px` main control
  area, `28 px` utility strip, and `160 px` expanded RTA.
- Preserve the continuous 4 px outer frame, 1 px internal dividers, exact
  cross-layer alignment, paper/ink inversion, zero radii, and embedded
  JetBrains Mono Medium 500 / ExtraBold 800 typography.
- Rebuild the header as `LOGO / ALGORITHM / OS / AUTO GAIN / POWER`; keep the
  full cell as the hit target and retain the approved popup lifecycle.
- Build the main area as a 4 x 3 matrix of 100 x 60 px cells beside stereo
  input/output meters and the existing saturation visualization.
- Use this exact control matrix:

  1. `DRIVE / CHARACTER / SECONDARY / ASYM + STEREO`
  2. `ROUTE / PLACEMENT / DYNAMIC / SPEED`
  3. `INPUT HP / TONE / STAGES / OUTPUT LP`

- Build the utility strip as `MULTIBAND ON / LINK / PHASE / MIX / OUT`. `LINK`
  and `PHASE` align to the third and fourth control columns; the visible
  `PHASE / MIX` stroke aligns pixel-for-pixel with `control / meters`.
- `MIX` remains contextual exactly as it is in the current Single/linked/unlinked
  model even though it moves to the strip. `OUT` remains one global final-output
  parameter.
- Keep every current production saturation visualization unchanged. Only its
  bounds, placement, framing, and paper/ink colors move into the new layout.
- Port the `default_eq` RTA one-to-one, then add only the distortion-specific
  crossover, Trim, band selection, S/B, ghost-crossover, and tooltip overlay.
- Keep the approved three-column algorithm menu, column-major order, production
  previews, screen-edge handling, and exact closed/open naming.

### Compatibility boundary

- Preserve every existing 0.8.x parameter ID, order, range, default, text
  conversion, automation behavior, and host gesture boundary.
- Preserve all 30 saturation algorithms, their DSP, Auto Gain calibration,
  oversampling, multiband crossover DSP, phase modes, latency, presets, project
  recall, and existing state migration.
- Preserve existing keyboard shortcuts, mouse buttons, modifiers, drag axes,
  wheel directions, double-click actions, contextual actions, and linked or
  unlinked band behavior unless this document explicitly adds a new action.
- Add new parameters without renumbering or replacing old parameters. Give every
  new parameter a stable ID and append it to the state schema.
- Loading an 0.8.x state initializes all new parameters to neutral values and
  reproduces the old audio output within the established DSP-equivalence budget.
- Do not change `DistortionEngine::makeVisualization` data or replace it with
  paths from the HTML prototype.
- Do not refactor unrelated DSP while migrating a visual region.

## New 0.9.0 processing

### Approved DSP decisions

- Signal flow: `Input -> Input HP -> routing/saturation -> Auto Gain -> Mix ->
  Output LP -> global Output`.
- `INPUT HP`: third-order Butterworth, 18 dB/oct, `OFF / 0 Hz` through
  `200 Hz`.
- `OUTPUT LP`: third-order Butterworth, 18 dB/oct, `2 kHz` through
  `20 kHz / OFF`.
- The Dynamic detector is a stereo-linked peak envelope follower fed by the
  complete raw plug-in input before routing or multiband splitting. Unlinked
  bands use independent follower state while receiving the same detector tap.
- `SPEED` uses the exact `default_eq` 0.5.3 logarithmic timing curve:
  `0% = 100/1000 ms`, `50% = 10/100 ms`, `100% = 0.1/15 ms`
  (attack/release).
- Smart Auto Gain retains its existing measurement lifecycle and observation
  taps; Dynamic does not turn it into a continuously chasing gain stage.

### M/S and T/S routing

- Add a two-state `ROUTE` selector: `M/S` and `T/S`.
- Add continuous `PLACEMENT` from `-100…0…+100%`:
  - `0%` processes the complete signal;
  - M/S `-100%` processes Mid only and `+100%` processes Side only;
  - T/S `-100%` processes Transient only and `+100%` processes Sustain only.
- Reuse the energy-preserving M/S encode/decode and placement weighting already
  proven in `default_eq`; verify centered reconstruction, mono compatibility,
  and absence of unintended level changes before adapting it.
- Adapt the `default_eq` ZLSplitter-derived transient/sustain engine instead of
  inventing a second implementation. Preserve complementary outputs, 75%
  overlap, warm/reset behavior, parameter transforms, and explicit latency.
- Preallocate every T/S FFT, window, delay, and work buffer in `prepareToPlay`.
  Perform no allocation or locking on the audio thread.
- Report T/S latency correctly, align dry/wet and Auto Gain observation paths,
  and make live route changes click-free. M/S must not incur T/S latency or FFT
  work while no T/S route is active.
- Add the upstream ZLSplitter provenance, revision, adapted-file boundary, and
  AGPL-3.0 notice to `THIRD_PARTY_NOTICES.md` and the relevant license records.

### Dynamic and Speed

- Add `DYNAMIC` in `-100…0…+100%` and `SPEED` in `0…100%` to the master context
  and every multiband saturation context.
- Implement the exact control law:

  `effective Drive = clamp(base Drive + envelope × Dynamic range, 0, 36 dB)`

- Map the maximum Dynamic depth to `±36 dB`:
  - `DYNAMIC = 0` is a strict neutral state and must reproduce the old sound;
  - negative Dynamic reduces Drive on loud passages, preserving peaks while the
    quieter body remains dense;
  - positive Dynamic increases Drive on loud passages for more aggressive attacks;
  - when the detector returns to silence, effective Drive returns to base Drive.
- Define and document the `SPEED` time mapping before final tuning. It must
  control detector motion smoothly over the entire range, remain sample-rate
  independent, and avoid discontinuities when automated.
- Choose and document the detector tap before implementation; the envelope must
  follow the signal feeding the relevant saturation context rather than a later
  output-level control. Keep detector state independent per unlinked band and
  avoid duplicating work in linked mode where a shared result is valid.
- Smooth Drive modulation without bypassing the existing parameter, automation,
  Auto Gain, oversampling, or stage behavior. No Dynamic state may write back into
  the base Drive parameter.
- Prevent denormals, NaN/Inf values, block-size dependence, and modulation beyond
  the existing `0…36 dB` Drive range.

### Input HP and Output LP

- Add logarithmic `INPUT HP` and `OUTPUT LP` cutoff parameters to the master
  context and every multiband saturation context.
- `INPUT HP` belongs before the saturation core and `OUTPUT LP` belongs after it.
  Decide and approve their exact relationship to contextual Mix, Auto Gain, and
  final Output before implementation; document the resulting order in the
  signal-flow diagram and test it explicitly.
- Both filters default to `OFF`. The HP endpoint at the bottom and LP endpoint at
  the top display `OFF`, bypass their filters completely, and reproduce the old
  path without residual coloration or unnecessary processing.
- Use logarithmic vertical drag, Shift fine adjustment, double-click numeric
  entry, `Hz`/`kHz` formatting away from the endpoint, and `OFF` at the neutral
  endpoint, matching the prototype.
- Choose and document filter topology, slope, cutoff range, smoothing, and
  Nyquist clamping before DSP implementation; do not infer them from the
  prototype drawing alone.
- Make automation click-free and stable at every supported sample rate and block
  size. Test endpoint bypass transitions, DC rejection, high-frequency rolloff,
  and repeated automation through `OFF`.

## Implementation sequence

- [x] Freeze the alternative HTML prototype and capture approved compact,
  expanded, light, dark, menu-open, and interaction-state reference images.
- [x] Inventory the complete 0.8.x public contract: parameters, state schema,
  presets, automation, shortcuts, mouse actions, multiband linking, latency,
  analyzer behavior, and all 30 visualization arrays.
- [x] Add baseline tests before production changes: parameter manifest, old-state
  migration, deterministic audio fixtures, visualization snapshots, latency,
  analyzer transport, and editor interaction/layout assertions.
- [x] Define the new parameter IDs, master/per-band ownership, ranges, defaults,
  formatting, schema migration, and host-gesture rules for Route, Placement,
  Dynamic, Speed, Input HP, and Output LP.
- [x] Implement and test M/S routing as an isolated DSP change with the old UI
  still intact.
- [x] Port and test the T/S splitter, latency, reconstruction, and routing as a
  separate change with full attribution.
- [x] Implement and test the Dynamic/Speed envelope follower as a separate neutral-
  by-default DSP change.
- [x] Implement and test Input HP and Output LP as separate neutral-by-default
  DSP changes after their topology and slope are approved.
- [x] Extract reusable family UI primitives and centralized design-space metrics
  before replacing the editor layout.
- [x] Migrate one visual region at a time: outer shell and font, header, control
  matrix, meters and saturation visualization, utility strip, RTA, then menus
  and overlays. Keep every step buildable and reviewable.
- [x] Port the current `default_eq` RTA numerically and visually one-to-one,
  preserving its lock-free transport and preallocated FFT resources, then layer
  the distortion interactions on top.
- [x] Connect every new and moved control through APVTS attachments and explicit
  begin/change/end host gestures. Verify Single, linked Multiband, and unlinked
  per-band rebinding after each region.
- [x] Add screen-level project-owned popup windows for algorithm, OS, Phase,
  crossover slope, and contextual menus from the start; do not rely on child
  components that can be clipped by the plug-in editor.
- [x] Remove old editor components only after feature, interaction, automation,
  accessibility, and state parity has passed for their replacement.

## Lessons from the default_eq interface migration

These are mandatory safeguards, not optional retrospective notes.

- Do not combine the entire migration into one release commit. The `default_eq`
  0.5.0 change touched 35 files with roughly 4,600 inserted lines, which made
  visual, behavioral, analyzer, and platform regressions difficult to isolate.
  Use small region- or feature-specific commits and run focused gates after each.
- Do not move the target while porting it. `default_eq` changed the HTML
  prototype and JUCE implementation in the same migration commit. Freeze the
  approved distortion prototype first; any later design change requires an
  explicit prototype revision, updated reference images, and a matching JUCE
  parity check.
- Do not translate CSS coordinates directly into scattered JUCE integers.
  Centralize the reference frame, column boundaries, pixel snapping, insets,
  typography, and scale conversion, then assert the visible strokes—not merely
  adjacent component bounds—at 1x and HiDPI scales.
- Do not treat static geometry or an offscreen snapshot as complete visual QA.
  Inspect the real AU/VST3 editor in representative hosts on macOS, Windows, and
  Linux. Verify embedded-font rasterization, clipping, dividers, menus, focus,
  cursors, and dark-theme inversion at minimum/default/2x/3x scales.
- Build popup menus as screen-level windows from the beginning. `default_eq`
  shipped child-level context menus that were clipped by the plug-in window and
  required the 0.5.3 repair. Test pointer-relative placement, edge flipping,
  submenu hover lifetime, outside dismissal, Escape, second-click toggle, and
  relayout while open.
- Test the complete selector state lifecycle. The EQ port exposed stuck pressed
  or keyboard-focus outlines, parent outside-click handling, and menus dismissed
  by internal relayout. Open state must exist only while the popup exists.
- Create a pre-migration gesture inventory and automate it. The EQ migration
  missed immediate drag after Shift-created filters and its single Undo gesture;
  the behavior was restored in 0.5.2. Cover creation, selection, drag, wheel,
  modifiers, reset, numeric entry, group edit, and cancel paths explicitly.
- Assert cross-layer lines and text baselines, not just enclosing rectangles.
  The EQ band row needed a 0.5.2 alignment correction despite the 0.5.0 layout
  suite. Include the exact physical pixel occupied by every structural divider.
- Keep analyzer labels and controls driven by the actual analyzer state. The EQ
  autoscale code remained alive while hard-coded grid labels hid its effect;
  reproduce each user interaction before declaring analyzer behavior correct.
- Separate tolerant platform rendering checks from design invariants. The EQ
  release needed platform-aware Windows rendering gates and a non-macOS shortcut
  label fix. Allow known raster differences, but never use broad tolerance to
  hide clipping, wrong text, missing controls, or shifted architecture.
- Update version metadata, hard-coded version tests, packaging, and installed
  plug-in caches together. EQ release tests twice retained the previous version,
  and AU validation once read a cached old component until the registrar was
  restarted.
- Distinguish every evidence layer: source checks, unit/CTest results, DSP
  equivalence, offscreen layout render, browser prototype render, built plug-in,
  installed AU/VST3, host interaction QA, CI artifacts, and published release.
  Passing one layer is never evidence that all later layers passed.

## Validation matrix

- [x] Prove neutral compatibility: old 0.8.x states load with Route/Placement,
  Dynamic, Input HP, and Output LP neutral and match the old audio, latency,
  visualization arrays, parameter values, and Auto Gain behavior.
- [x] Test M/S reconstruction, Mid-only, Side-only, intermediate placement,
  mono input, polarity, asymmetric stereo, linked/unlinked bands, and automation.
- [x] Test T/S complementarity, latency and PDC, silence, impulses, sustained
  tones, transients, transport restart, reset, sample-rate changes, route changes,
  and combined T/S plus oversampling/multiband/phase operation.
- [x] Test Dynamic at `-100/0/+100%`, Drive at `0/36 dB`, silence return, step and
  ramp automation, all Speed values, every sample rate/block size, all algorithms,
  all stage counts, and linked/unlinked band contexts.
- [x] Test HP/LP `OFF`, first active step, representative cutoffs, extremes,
  automation through bypass, sample rates through 192 kHz, and response accuracy.
- [x] Compare all 30 production visualization outputs before and after migration;
  bounds and colors may change, numerical transfer data may not.
- [x] Feed identical deterministic signals into `default_eq` and
  `default_distortion`; compare RTA bins, smoothing, peaks, grid values, update
  cadence, and rendered paths within documented numeric and pixel tolerances.
- [ ] Cover compact/expanded, light/dark, Single/Multiband, linked/unlinked,
  2/3/4 bands, all algorithm-dependent labels, disabled controls, every popup,
  minimum/default/2x/3x scale, and macOS/Windows/Linux rendering.
- [x] Run Release builds, the complete CTest suite, pluginval strictness 10,
  `auval` on macOS, state/automation fuzzing, memory and CPU baselines, no-audio-
  thread-allocation checks, and representative DAW interaction QA.

## 0.9.0 completion gate

- The JUCE editor matches the frozen alternative prototype at the visible-pixel
  level and uses the shared `default_` design system.
- Every existing 0.8.x state, preset, automation lane, shortcut, and mouse action
  retains its behavior unless explicitly extended above.
- `DYNAMIC = 0`, `INPUT HP = OFF`, and `OUTPUT LP = OFF` are demonstrably neutral.
- M/S and T/S routing, Dynamic/Speed, and both filters satisfy their DSP, latency,
  automation, and state tests.
- Saturation visualization data is unchanged; the RTA matches `default_eq` 0.5.3
  one-to-one apart from its placement and distortion-specific overlays.
- No label clips, no menu is trapped by the editor, no active state sticks after
  dismissal, and structural lines remain aligned at every supported scale.
- Cross-platform builds and validation pass before screenshots, changelog,
  packages, tag, installation, or publication are prepared.

## Resolved DSP decisions

- Input HP and Output LP use third-order Butterworth filters at 18 dB/oct.
- Input HP is `OFF/0…200 Hz`; Output LP is `2 kHz…20 kHz/OFF`.
- The approved signal path is documented in `docs/architecture-0.9.md`.
- Speed uses the approved `default_eq` 0.5.3 attack/release curve documented
  above and in `docs/architecture-0.9.md`.
