# default_ Visual Design System

Document status: normative

Scope: the complete default_ audio plug-in family

Last updated: 7 September 2026

## 1. Purpose

This document defines the shared visual and interaction language of default_ products
and the target design of every interface in the family.

A conforming default_ interface must feel like a precise physical instrument:

- compact and function-dense;
- monochrome and geometric;
- direct to operate;
- technically explicit;
- visually forceful without decorative noise;
- honest about the product's real parameters and signal flow.

The family identity is created by composition, mass, typography, state, and
interaction. It is not a skin placed over arbitrary controls.

## 2. Core design statement

> A compact monochrome audio instrument inside a continuous black frame: functional
> visualization, a strict cross-layer grid, direct manipulation, and no decorative
> interface chrome.

The interface should feel:

- poster-like in its distribution of black and paper masses;
- technical in typography and numeric precision;
- sharp in geometry;
- calm in its use of effects;
- immediate in operation;
- brutal through weight, not through visual clutter.

## 3. Rule hierarchy

### 3.1. Family invariants

Every default_ product uses:

- a wordmark in the form default_<function>;
- the paper / ink palette;
- exact light/dark role inversion;
- embedded JetBrains Mono typography;
- zero corner radius;
- rectangular zones and square handles;
- full inversion for active binary states;
- functional visualization instead of decorative imagery;
- direct manipulation wherever a parameter is already represented visually;
- short uppercase interface labels and correct domain units;
- fixed composition with proportional scaling;
- no gradients, glow, soft shadows, glass, or dimensional materials.

### 3.2. Adaptable family patterns

A product may adapt:

- header proportions;
- the number and order of global functions;
- the type and size of the primary visualization;
- the presence of a value strip;
- the organization of a contextual workspace;
- the number of alignment anchors;
- menu dimensions and specialized controls;
- the balance between a visualization-first layout and a module-first layout.

Adaptation must preserve the family invariants and the product's complete functional
model.

### 3.3. Product-specific rules

Each product defines:

- its actual parameter set;
- parameter ranges, defaults, units, and precision;
- its domain scale;
- its meaningful alignment anchors;
- its visualization;
- its context-menu actions;
- grouped-editing behavior;
- product-specific icons and terminology.

No family rule authorizes invented parameters, renamed concepts, removed states, or
decorative functions that do not exist in the product.

### 3.4. Interface-only migration contract

When a product is assigned an interface-only redesign, the visual system may change
composition, geometry, typography, styling, and the placement of existing controls.
It must preserve the production product beneath that interface:

- audio DSP, signal flow, coefficients, oversampling, latency, and CPU behaviour;
- parameter IDs, ordering, ranges, defaults, normalization, text conversion, and
  automation;
- preset and project state, migration, Undo/Redo, and host gesture boundaries;
- analyzer and RTA acquisition, FFT, smoothing, decay, calibration, cadence, and
  response calculations;
- every keyboard shortcut, modifier, mouse action, drag axis, wheel action,
  double-click action, context-menu action, and multi-selection rule;
- accessibility semantics and focus behaviour.

An HTML prototype is the authority for approved visual composition and styling. It
is not permission to replace production processing or interaction code. If prototype
behaviour differs from the shipping product, existing production behaviour wins until
a separate behavioural change is explicitly approved.

## 4. Palette and contrast

| Token | Value | Purpose |
|---|---:|---|
| paper | #F6F6F6 | Primary surface and inverted text |
| ink | #050505 | Frame, active mass, primary text, and lines |
| label-muted | foreground at 0.72 alpha | Secondary labels and units |
| disabled | foreground at approximately 0.32 alpha | Disabled controls |
| analyzer-input | foreground at approximately 0.20 alpha | Secondary analyzer trace |
| analyzer-output | foreground at approximately 0.48 alpha | Primary analyzer trace |
| selection-halo | foreground at approximately 0.07 alpha | Compact selected-object halo |

Rules:

- active slider fill is opaque ink;
- active ON, SOLO, POWER, and equivalent actions are solid ink with paper text;
- muted information must never resemble an active control;
- color does not encode function;
- dark theme swaps the roles of paper and ink;
- #FFFFFF and #000000 must not replace the family tokens;
- analyzer gray is reserved for analysis and secondary signal information;
- disabled gray is reserved for unavailable interaction.

## 5. Composition and geometry

### 5.1. Structural hierarchy

The visual hierarchy is built from four levels:

1. a continuous outer frame;
2. structural black gaps between major layers;
3. thin internal dividers;
4. solid active masses.

Do not make every boundary heavy. Brutality comes from deliberate mass and contrast,
not from uniformly thick strokes.

### 5.2. Standard dimensions

- outer architectural frame: 4 design px;
- internal divider: 1 design px;
- major horizontal gap: 4 design px or a deliberately larger black mass;
- micro-spacing: 2, 3, 4, 6, and 8 design px;
- corner radius: 0;
- static 1 px lines must land on physical pixels at 1x.

### 5.3. Continuous frame

The outer frame:

- closes the window on all four sides;
- reaches all four corners;
- continues past adjacent header cells;
- is never interrupted by the wordmark, power control, or visualization;
- remains visually continuous at every supported scale.

### 5.4. Cross-layer alignment

Major vertical boundaries should continue through adjacent layers whenever they
represent related structure.

The process is:

1. identify meaningful domain landmarks;
2. calculate their coordinates from the real scale;
3. place major boundaries on those coordinates;
4. align header, visualization, value strip, and workspace boundaries to them;
5. allow unequal column widths when they strengthen the semantic grid;
6. avoid decorative columns created only to force symmetry.

Frequency, time, gain, phase, or spatial landmarks are preferable to arbitrary
pixel choices.

### 5.5. Composition archetypes

Visualization-first products use:

- a compact header;
- one dominant functional visualization;
- a narrow selected-value strip;
- a compact contextual workspace.

Module-first products use:

- a compact header;
- a stable matrix of processing modules;
- shared rectangular controls;
- expansion only when additional controls are truly contextual.

Both archetypes must use the same palette, state language, typography, frame,
rectangular control geometry, and direct-manipulation rules.

## 6. Header

### 6.1. Structure

The header contains:

- the default_<function> wordmark at the left edge;
- product-level selectors and continuous controls;
- mode or utility actions;
- master power at the right edge.

Global controls must align to meaningful boundaries in the layers below whenever
possible.

### 6.2. Wordmark

- use lowercase snake_case;
- use the same heavy monospace family as the interface;
- optically center the wordmark inside its cell;
- keep the outer frame visible beside it;
- treat any square seam marks as part of the composition;
- theme switching may be assigned to the wordmark when the product supports it.

### 6.3. Header selectors

- the complete cell is the hit target;
- opening is not restricted to the visible text;
- the open state exists only while the menu is visible;
- selecting an option clears the open state immediately;
- dismissing the menu without a selection also clears the open state immediately;
- keyboard focus must not look like a stuck pressed state;
- label and value form one compact vertical pair;
- text never clips against the cell boundary.

### 6.4. Header continuous controls

Continuous header cells use relative vertical drag:

- single click does not change the value;
- dragging begins from the current value;
- upward motion increases and downward motion decreases;
- the cursor is ns-resize;
- Shift enables fine adjustment;
- sensitivity matches related controls elsewhere in the product.

## 7. Functional visualization

A dominant visualization must:

- show a real cause-and-effect relationship;
- accept direct input;
- preserve selected-object context;
- distinguish primary and secondary data without color;
- expose precise or structural actions through a contextual menu;
- remain useful when multiple objects are selected.

Primary data uses stronger contrast than analyzer or reference data.

### 7.1. Object markers

- markers are square and visually heavy;
- selected markers may use a compact low-alpha halo;
- no glow or circular point handles;
- the invisible hit target is substantially larger than the visible marker;
- the minimum practical hit target is 24 × 24 physical px;
- direct dragging uses grab/grabbing or the domain-appropriate cursor.

### 7.2. Multi-selection

- one primary object supplies the displayed reference value;
- compatible continuous edits preserve relative offsets;
- discrete choices apply to the complete selection;
- the available control set does not collapse or change shape;
- mixed values display MULTI;
- no decorative selected-object number panel is added when selection is already
  visible in the visualization.

## 8. Value cells and value strips

A value cell combines:

1. a concise label;
2. a formatted value;
3. a direct-manipulation surface;
4. manual entry when precision requires it.

Behavior:

- vertical drag is the primary adjustment;
- Shift + drag provides fine adjustment;
- double-click enters numeric editing;
- single click does not enter text mode;
- single click does not jump the value;
- linear parameters move linearly;
- logarithmic parameters move logarithmically;
- the cursor communicates the drag axis;
- displayed precision matches useful parameter precision;
- units remain visible without becoming dominant.

A discrete selector inside a value strip may omit a redundant label when its icon
and selected value already communicate the function.

## 9. Full-cell selectors and popup menus

### 9.1. Hit area

- every point inside the selector cell opens the menu;
- no click target is limited to glyphs or text;
- the selector face remains aligned with neighboring value cells;
- opening the menu must not shift layout.

### 9.2. Theme

Popup menus use only paper and ink:

- menu background: paper;
- menu text: ink;
- active, hovered, and keyboard-highlighted rows: ink with paper text;
- border: ink;
- the same role mapping applies in dark theme;
- system #FFFFFF is not used;
- colored native selection is not used;
- text and background always invert together.

### 9.3. State lifecycle

- the selector cell indicates open state only while its popup exists;
- selecting an item closes the popup and clears open state;
- clicking outside closes the popup and clears open state;
- Escape closes the popup and clears open state;
- closed focus and open/pressed state remain visually distinct;
- reopening rebuilds selected-state indication from the current parameter value.

### 9.4. Product fidelity

- menu contents match the real product;
- rare actions may live in a contextual menu;
- frequent controls remain directly accessible;
- no filler actions are added;
- submenus choose a side based on pointer position and available screen space.

## 10. Vertical sliders

The canonical vertical slider:

- occupies the useful height of its cell;
- uses a rectangular track approximately 22 design px wide;
- uses an opaque fill approximately 18 design px wide;
- fills from bottom to top;
- centers track and fill in the column;
- uses the complete cell as its drag surface;
- uses relative vertical drag;
- does not react to a stationary click;
- shows an ns-resize cursor;
- uses approximately 0.32 alpha for disabled fill;
- has no round thumb.

Reduce track height only together with surrounding padding. Do not leave a large
empty cell around an artificially short control.

Related vertical sliders should use compatible drag distances and sensitivity.

## 11. Threshold meter-fader

Threshold is a combined meter and fader, not a standard slider hidden beneath a
meter graphic.

Construction:

- two independent vertical rectangular meter lanes;
- no shared outline around the two lanes;
- a narrow gap between lanes;
- opaque active meter fill;
- one explicit horizontal threshold line;
- full drag range mapped to approximately the visible meter height;
- the complete cell accepts relative vertical drag;
- Shift enables fine adjustment;
- no round handle.

The threshold interaction must remain as responsive as neighboring controls.

## 12. Buttons and binary state

### 12.1. Default

- paper background;
- ink text and line;
- clear 1 px separation from adjacent cells.

### 12.2. Active

- solid ink background;
- paper text;
- no gray substitute for active state;
- no dependence on outline alone.

### 12.3. Disabled

- preserve the control's geometry;
- reduce foreground to approximately 0.32 alpha;
- do not make disabled controls resemble analyzer data.

Related action pairs such as ON/SOLO and processing-mode/source actions should use
the same construction and spacing.

## 13. Typography

The family typeface is JetBrains Mono v2.304, distributed under the SIL Open Font
License 1.1. Every shipped interface embeds the exact upstream font binary rather
than resolving a similarly named system font.

- HTML prototypes use the upstream Medium 500 and ExtraBold 800 WOFF2 files.
- JUCE products embed the matching static TTF files as binary data.
- The family UI uses only the supplied weights 500 and 800; synthetic bold and
  italic are disabled.
- Ligatures are disabled for stable labels, values, and measurements.
- A generic monospace fallback may remain only as a load-failure safeguard; it is
  not part of the approved rendering.
- Every repository that redistributes the font includes its OFL 1.1 license and
  records the upstream release and revision in third-party notices.
- JetBrains Mono is the family typeface for every default_ plug-in; individual
  products do not substitute another monospace face.

Reference scale at 1x:

| Role | Size | Weight / contrast |
|---|---:|---|
| Wordmark | 20 px | 800 |
| Header label | 9 px | 800, uppercase |
| Header value | 11–13 px | 800 |
| Value-strip label | 9 px | 800, 0.72 alpha |
| Unit | 9 px | 800, 0.72 alpha |
| Value-strip value | 10 px | 800 |
| Workspace label | 9 px | 800, 0.72 alpha |
| Workspace value or button | 9 px | 800 |
| Visualization grid label | product-scaled | 800, 0.72 alpha |

Rules:

- interface labels are uppercase;
- wordmarks use lowercase snake_case;
- labels do not wrap;
- shorten terminology before reducing type size;
- do not shrink one label into illegibility to preserve a faulty column;
- numeric precision never exceeds meaningful parameter precision;
- units use standard case: Hz, dB, dB/oct, ms;
- neighboring controls at the same hierarchy use the same label size and contrast;
- secondary labels must remain clearly readable rather than decorative gray;
- ordinary labels, units, selector values, popup rows, and tooltips are never smaller
  than 9 design px at the 1x reference scale;
- a closed selector and its popup rows use the same apparent type size;
- wordmarks, controls, menus, values, units, tooltips, and visualization labels use
  the same embedded JetBrains Mono family.

## 14. Icons

- use the product's real production icons;
- preserve the icon vocabulary and number of available types;
- do not redraw icons merely in the style of the originals;
- use currentColor so icons follow theme inversion;
- align response icons by the unaffected-response baseline, not by the bounding box
  of the complete curve;
- for a low shelf, center by the right flat section;
- for a high shelf, center by the left flat section;
- icon hit targets belong to the complete selector cell.

## 15. Labels, abbreviations, and clipping

Terminology must stay faithful to the product. Shortening is permitted only to make
the existing meaning legible.

Rules:

- prefer established audio terminology;
- preserve units and direction;
- abbreviate before reducing font size;
- use one consistent abbreviation everywhere;
- align related labels consistently;
- ensure every label fits at minimum, default, and maximum scale;
- do not clip text that would fit with correct padding or alignment;
- inherited `overflow: hidden` or ellipsis must not crop a label that fits inside its
  cell including available padding;
- do not center a subgroup label when neighboring selectors are left-aligned.

## 16. Context menus and tooltips

### 16.1. Context menus

- reproduce the product's real action set;
- preserve the original grouping and available filter or processing types;
- apply actions to multi-selection according to product semantics;
- keep the menu inside available display bounds;
- position submenus according to pointer side and free space;
- use the same paper / ink state language as the interface.

### 16.2. Tooltips

- show real product values only;
- use the product's formatting, precision, and units;
- do not invent descriptive parameters;
- do not hide the selected object's primary controls;
- remain compact;
- follow the current theme.

## 17. Brutalist character

The family obtains visual force from:

- the continuous 4 px frame;
- large paper surfaces held inside black mass;
- solid black active blocks;
- opaque slider fills;
- wide rectangular meters;
- heavy square object markers;
- concise labels;
- strong scale contrast;
- rare square seams and notches;
- disciplined empty space around dense controls.

Do not create force through:

- uniformly thick dividers;
- outlines tightly wrapped around text;
- decorative noise;
- glow;
- oversized labels;
- arbitrary asymmetry;
- reduced functional visualization area;
- gray active controls;
- excessive vertical stretching of contextual panels.

## 18. Interaction priority

Use this order:

1. drag the object in the visualization;
2. drag the corresponding value cell;
3. double-click for numeric entry;
4. use a selector for discrete choices;
5. use a context menu for structural, rare, or reset actions.

The most frequent operation should require the fewest layers.

## 19. Host and automation semantics

In a production plug-in, every parameter drag must:

- begin a host parameter gesture;
- send parameter changes to the host;
- end the gesture on pointer up or pointer cancel;
- remain stable under automation;
- update without feedback loops;
- preserve grouped-editing semantics;
- keep audio state separate from theme and other UI preferences.

Prototype behavior must map cleanly to these semantics even when the prototype does
not implement a host.

## 20. Scaling

- preserve the complete composition; do not reflow it into a different interface;
- scale the design space proportionally;
- store major boundaries and anchors centrally;
- snap lines to physical pixels;
- maintain a minimum 24 × 24 physical px hit target;
- scale popup and tooltip geometry with their targets;
- recheck text clipping at every supported scale;
- calculate domain landmarks from the real scale;
- verify that the embedded JetBrains Mono assets load on macOS, Windows, and Linux;
- test text clipping and rasterization on all three systems even though glyph metrics
  are supplied by the same embedded font.

## 21. Shared implementation primitives

A shared JUCE family layer should expose primitives equivalent to:

~~~text
FamilyPalette
FamilyMetrics
FamilyFrame
FamilyWordmarkButton
FamilyHeaderCell
FamilyActionButton
FamilyFullCellSelector
FamilyValueDragField
FamilyVerticalSlider
FamilyThresholdMeterFader
FamilyObjectMarker
FamilyContextMenu
FamilyTooltip
FamilyScaleContext
~~~

Centralize design-space metrics:

~~~cpp
struct FamilyMetrics
{
    static constexpr float frame = 4.0f;
    static constexpr float thinLine = 1.0f;
    static constexpr float minimumHitTarget = 24.0f;
};
~~~

The product layer supplies:

- design width and height;
- parameters and formatting;
- domain scale;
- alignment anchors;
- specialized visualization;
- icons;
- context-menu actions;
- grouped-editing rules.

## 22. Prohibited patterns

- round knobs or round slider thumbs;
- rounded cards or pill buttons;
- product differentiation through arbitrary accent colors;
- gray active controls;
- native selectors whose hit target is limited to text;
- colored native menu selection;
- popup colors that fail to invert in dark theme;
- selector cells that remain visually pressed after the popup closes;
- focus rectangles tightly wrapped around text;
- horizontal drag on visually vertical controls;
- absolute click-to-position behavior on relative drag cells;
- numeric fields without drag control;
- single-click text editing that conflicts with direct manipulation;
- invented parameters, labels, tooltip data, or menu actions;
- decorative selected-object panels when selection is already clear;
- unrelated vertical boundaries between neighboring layers;
- decorative visualization that does not represent signal or state;
- uniformly heavy internal borders;
- clipped labels;
- reduced hit targets around visible object markers.

## 23. QA checklist

### Geometry

- [ ] The outer frame is continuous on all four sides.
- [ ] Header, visualization, value strip, and workspace remain inside the frame.
- [ ] Major X boundaries align across layers.
- [ ] Domain anchors are mathematically calculated.
- [ ] Internal lines render at 1 physical px at 1x.
- [ ] No accidental subpixel dividers exist.
- [ ] No label or value is clipped.

### Visual state

- [ ] Active fills use full foreground.
- [ ] Analyzer and disabled state remain secondary.
- [ ] No glow, gradients, soft shadows, or radius appear.
- [ ] Object handles are square and heavier than the selection halo.
- [ ] Threshold uses two meter rectangles without a shared outline.
- [ ] Light and dark themes invert paper and ink roles exactly.
- [ ] Closed selectors do not look pressed.
- [ ] Closed selectors match their popup rows in type scale and icon legibility.
- [ ] No inherited overflow rule crops otherwise fitting text.

### Interaction

- [ ] The complete selector cell opens its menu.
- [ ] Selector popup selection is monochrome and readable in both themes.
- [ ] Choosing an item clears the selector's open state.
- [ ] Dismissing a popup clears the selector's open state.
- [ ] Keyboard focus is distinct from open/pressed state.
- [ ] Every visually vertical control responds to vertical drag.
- [ ] A stationary click on a relative slider does not change its value.
- [ ] Numeric value cells respond to vertical drag.
- [ ] Double-click enables manual numeric entry.
- [ ] Shift provides fine adjustment.
- [ ] Multi-selection preserves intended relative changes.
- [ ] Context menus match the real product.
- [ ] Tooltips match real values and units.

### Production

- [ ] Host gestures have correct begin and end boundaries.
- [ ] Automation updates the UI without conflicts.
- [ ] UI preferences do not enter audio parameter state.
- [ ] Minimum, default, and maximum scale have been checked.
- [ ] The embedded JetBrains Mono assets load without falling back on macOS, Windows,
      and Linux.
- [ ] Text clipping and rasterization have been checked on all three systems.
- [ ] Mouse, trackpad, and keyboard editing have been checked.
- [ ] Interface-only migrations preserve DSP, RTA/analyzer calculations, parameters,
      automation, state, shortcuts, and mouse behaviour against the production
      baseline.

## 24. default_eq reference profile

This profile records one complete conforming layout. Its parameter names and exact
coordinates are product-specific; its structural method is reusable.

### 24.1. Design space

| Zone | X | Y | W | H |
|---|---:|---:|---:|---:|
| Complete window | 0 | 0 | 752 | 454 |
| Top frame | 0 | 0 | 752 | 4 |
| Header | 4 | 4 | 744 | 60 |
| Upper structural gap | 0 | 64 | 752 | 4 |
| RTA / response | 4 | 68 | 744 | 254 |
| RTA / values gap | 4 | 322 | 744 | 4 |
| Value strip | 4 | 326 | 744 | 28 |
| Structural gap | 0 | 354 | 752 | 4 |
| Contextual workspace | 4 | 358 | 744 | 92 |
| Bottom frame | 0 | 450 | 752 | 4 |

The left and right frame are also 4 design px.

### 24.2. Alignment anchors

| Absolute X | Aligned boundaries |
|---:|---|
| 53.5 | ON/SOLO · ROUTE; midpoint between the RTA left edge and 50 Hz |
| 102.5 | 50 Hz; ROUTE · DIST; ADAPTIVE Q · FILTER |
| 214.5 | midpoint between 100 and 200 Hz; DIST · DRIVE/CHARACTER; FILTER · FREQUENCY |
| 350.5 | 500 Hz; PHASE · AMOUNT; FREQUENCY · GAIN; CHARACTER · DYNAMICS |
| 462.5 | midpoint between 1 and 2 kHz; AMOUNT · SHIFT; GAIN · Q; THRESHOLD · RANGE |
| 530.5 | Q · SLOPE; RANGE · RATIO |
| 598.5 | 5 kHz; SHIFT · AUTO GAIN; RATIO · SPEED |
| 673.5 | 10 kHz; AUTO GAIN · POWER; SLOPE · OUT; SPEED · LOOKAHEAD |
| 748 | Inner composition right edge |

### 24.3. Header grid

~~~text
174 | 74 | 99 | 112 | 136 | 75 | 74
LOGO | OS | PHASE | AMOUNT | SHIFT | AUTO GAIN | POWER
~~~

AMOUNT and SHIFT use relative vertical drag. OS and PHASE use full-cell selectors.
AUTO GAIN uses the standard 13 px left inset; AUTO GAIN and its current mode share
one left alignment axis. Phase modes are labelled MINIMUM, LINEAR ECO, LINEAR MED,
and LINEAR MAX.

### 24.4. Visualization

- logarithmic 20 Hz–20 kHz scale;
- input and output RTA traces;
- high-contrast EQ response;
- square band markers;
- approximately 20 physical px invisible marker radius;
- multi-selection and grouped editing;
- no redundant analyzer label or selected-band text block.
- the +12 and -12 scale labels use optically equal frame insets; edge-label
  placement may be corrected without moving the scale, grid, response, or nodes.

The production RTA and response implementation is retained intact during the UI
migration. Its acquisition path, FFT and smoothing, decay, normalization, calibration,
frequency mapping, response math, refresh cadence, and interaction semantics are not
reimplemented from the HTML prototype. The redesign may only place, frame, and style
the existing visualization inside the approved composition.

Filter icons use the production filter geometry. Their unaffected-response baseline
defines vertical alignment.

### 24.5. Value strip

~~~text
99 | 112 | 136 | 112 | 68 | 143 | 74
ADAPTIVE Q | FILTER | FREQUENCY | GAIN | Q | SLOPE | OUT
~~~

The FILTER cell displays the production icon and selected type without an extra
FILTER caption.

- the closed selector and popup row use the same 26 × 16 design px production icon,
  9 px value text, 7 px icon-to-text gap, and 10 px effective left inset;
- pass filters are named by what they pass: RES LP, RES HP, LOW PASS, and HIGH PASS.

Display precision:

| Parameter | Display |
|---|---|
| Frequency | Integer Hz |
| Gain | Exactly 1 decimal place |
| Q | Up to 2 decimal places |
| Slope | Product slope step |
| Out | Up to 1 decimal place |

Frequency, Gain, Q, Slope, and Out support vertical drag and double-click numeric
entry.

### 24.6. Contextual workspace

~~~text
50 | 49 | 247 | 1 | 397
STATE | ROUTING | SATURATION | GAP | DYNAMICS
~~~

Internal grids:

~~~text
SATURATION: 112 | 135
DIST TYPE   | DRIVE + CHARACTER

DYNAMICS: 50 | 62 | 285
ACTIONS     | THRESHOLD | PARAMETERS

PARAMETERS: 68 | 68 | 75 | 74
RANGE       | RATIO | SPEED | LOOKAHEAD
~~~

Rules:

- ON/SOLO use the same construction as DOWN/IN SC;
- ROUTE, DIST TYPE, filter type, PHASE, and OS use full-cell selectors;
- DRIVE, CHARACTER, RANGE, RATIO, SPEED, and LOOKAHEAD use the control orientation
  shown by their visual track;
- threshold is a dual-lane meter-fader;
- threshold values always include dB, except the explicit MULTI mixed state;
- compact Character labels use HYST and ODD/EVEN where the full term would clip;
- placement labels are left-aligned like ROUTE;
- centered L/R and M/S placement display CENTER;
- centered transient/sustain placement displays SUM;
- negative transient placement displays TRNSNT;
- placement uses relative vertical drag;
- placement, amount, and shift do not jump on click;
- placement, amount, and shift use the ns-resize cursor;
- labels and values remain inside their columns at every supported scale.
- THRESHOLD and SUSTAIN are never cropped by inherited overflow rules when they fit
  inside their cells.

### 24.7. Menus

- phase, filter-type, saturation-type, route, and oversampling menus use custom
  paper / ink rendering;
- no system white or colored selection appears;
- menu theme remains readable in light and dark modes;
- the complete field opens the menu;
- choosing or dismissing clears the open-state outline;
- filter menus show the production filter icons;
- closed selector values and popup rows use the same apparent 9 px type scale;
- the context menu reproduces the product's real action set.

## 25. default_distortion reference profile

This profile records the approved interface composition for the 0.9.0 visual
migration. Production saturation drawings and audio behaviour remain authoritative.

### 25.1. Design space

| Zone | X | Y | W | H |
|---|---:|---:|---:|---:|
| Compact window | 0 | 0 | 712 | 382 |
| Expanded window | 0 | 0 | 712 | 562 |
| Header | 4 | 4 | 704 | 60 |
| Saturation visualization | 4 | 68 | 704 | 230 |
| Value strip | 4 | 302 | 704 | 48 |
| Multiband strip | 4 | 354 | 704 | 24 |
| Expanded RTA | 4 | 382 | 704 | 176 |

All exterior edges use the continuous 4 px family frame. The 4 px gaps between
major horizontal regions remain visible as structural ink.

### 25.2. Header and value grids

~~~text
185 | 255 | 79 | 106 | 79
LOGO | ALGORITHM | OS | AUTO GAIN | POWER
~~~

The LOGO / ALGORITHM boundary aligns with the first vertical guide of the
saturation visualization. The OS / AUTO GAIN boundary aligns with its third guide.
The wordmark remains optically centred inside its enlarged cell. OS and POWER keep
equal widths; AUTO GAIN remains large enough for REGULAR, SMART, and OFF.

The parameter strip contains eight equal 88 px cells:

~~~text
DRIVE | CHARACTER | DETAIL | ASYM | TONE | STAGES | MIX | OUT
~~~

The contextual DETAIL label and availability follow the selected production
algorithm. Production transfer visualizations remain unchanged; only their bounds,
framing, palette integration, and placement may change.

### 25.3. Header and multiband selectors

- OS, algorithm, Phase, and crossover-slope selectors open from their complete
  cells and toggle closed on a second press;
- Auto Gain uses AUTO GAIN with REGULAR, SMART, and OFF;
- the closed Phase control and its popup use PHASE MINIMUM and PHASE LINEAR;
- MULTIBAND, LINK, and PHASE occupy a 504 / 88 / 112 px strip;
- adjacent active MULTIBAND and LINK cells retain a visible paper separator.

### 25.4. Multiband RTA

The production interface ports the default_eq analyzer appearance and behaviour
one-to-one, then adds only distortion-specific interaction overlays.

- each band contains vertically stacked S and B controls with 18 × 16 design px
  faces and minimum 24 × 24 physical px hit targets;
- the top edge of each S face aligns with the first horizontal RTA guide;
- band numbers and selected-band top bars are omitted;
- Trim uses relative vertical drag and double-click reset;
- crossovers use horizontal drag and double-click deletion;
- the crossover slope badge and bottom-anchored frequency tooltip appear only while
  that crossover is hovered or actively dragged;
- the frequency tooltip follows the crossover and updates continuously during drag;
- the upper half previews a valid new crossover at 25% opacity and adds it on click
  until four bands exist; the lower half selects, and all clicks select at four bands.

### 25.5. Algorithm menu

The algorithm menu spans the complete 704 px inner width, extends from the top of
the saturation visualization to the bottom of the parameter strip, and uses a
3 × 10 grid with 9 px text. It retains every production algorithm preview and has
no duplicate rule at its bottom edge.

## 26. Compact brief for a default_ product

> Design the interface as a fixed monochrome audio instrument inside a continuous
> 4 px frame. Use #F6F6F6 paper, #050505 ink, embedded JetBrains Mono typography,
> 1 px internal dividers, and rectangular active masses. Place the default_<function>
> wordmark at the left of the header and master power at the right. Make the primary
> visualization functional and directly manipulable. Align adjacent layers to a
> small set of meaningful domain anchors. Numeric fields use relative vertical drag,
> Shift fine adjustment, and double-click entry. Selectors open from the complete
> cell, remain monochrome in both themes, and clear their open state when dismissed.
> Vertical sliders use wide opaque fills without round thumbs. Use only real product
> parameters, actions, icons, values, and tooltips. Do not use decorative color,
> gradients, glow, rounded controls, gray active states, or invented functionality.
