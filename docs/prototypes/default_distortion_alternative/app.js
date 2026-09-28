(() => {
  "use strict";

  const NS = "http://www.w3.org/2000/svg";
  const MIN_FREQ = 20;
  const MAX_FREQ = 20000;
  const RTA_W = 640;
  const RTA_H = 160;
  const RESPONSE_W = 180;
  const RESPONSE_H = 182;
  const RTA_GUIDE_Y = [RTA_H * 0.164, RTA_H * 0.5, RTA_H * 0.836];

  const MODES = [
    { name: "SOFT CLIP", character: "CURVE" },
    { name: "HARD CLIP", character: "SOFTNESS" },
    { name: "DIODE", character: "TOPOLOGY" },
    { name: "TRIODE", character: "BIAS", bipolar: true },
    { name: "TRANSISTOR", character: "GATE", bipolar: true },
    { name: "TAPE", character: "HYSTERESIS", secondary: "BIAS", secondaryDefault: 0.5, defaultCharacter: 0.5, time: true },
    { name: "ODD / EVEN", character: "ODD/EVEN", bipolar: true },
    { name: "PHASE DISTORTION", character: "TONE", defaultCharacter: 0.5, time: true },
    { name: "SPECTRAL CLIP", character: "KNEE", time: true },
    { name: "SINE EROSION", character: "FREQUENCY", secondary: "NOISE", defaultCharacter: 0.5, time: true },
    { name: "SIGN / SQUARE", character: "THRESHOLD", bipolar: true },
    { name: "ZERO-SQUARE", character: "DEAD ZONE" },
    { name: "FULL-WAVE RECTIFIER", character: "RECTIFY", defaultCharacter: 0.5 },
    { name: "SOFT FULL-WAVE", character: "SOFTNESS" },
    { name: "TRANSFORMER CORE", character: "CORE", secondary: "AIR GAP", time: true },
    { name: "CLASS-B SATURATION", character: "BIAS GAP", defaultCharacter: 0.5 },
    { name: "TOPOLOGY FOLD", character: "TOPOLOGY", stepped: true },
    { name: "RECURSIVE FOLDBACK", character: "REFLECTION" },
    { name: "SINE FOLD", character: "CURVATURE" },
    { name: "CHEBYSHEV FOLD", character: "ORDER" },
    { name: "MODULO WRAP", character: "PERIOD" },
    { name: "DOWNSAMPLE", character: "SMOOTHING", secondary: "JITTER", time: true },
    { name: "BIT CRUSHER", character: "SMOOTHING", secondary: "DITHER" },
    { name: "BIT ROTATION", character: "ROTATION" },
    { name: "DELTA CRUSHER", character: "STEP", defaultCharacter: 0.5, time: true },
    { name: "SLEW LIMITER", character: "RATE", time: true },
    { name: "SCHMITT HYSTERESIS", character: "LOOP WIDTH", secondary: "SLEW" },
    { name: "FEEDBACK SATURATOR", character: "FEEDBACK", bipolar: true, time: true },
    { name: "RESONANT FEEDBACK CLIP", character: "COLOR", time: true },
    { name: "DYNAMIC SAG", character: "RECOVERY", time: true }
  ];

  const QUALITY = ["OFF", "2×", "4×", "8×"];
  const PHASES = ["MINIMUM", "LINEAR"];
  const SLOPES = [6, 12, 24, 36, 48];
  const CONTEXT_KEYS = [
    "mode", "drive", "character", "secondary", "asym", "asymStereo",
    "placementMode", "placement", "dynamic", "speed", "tone", "stages",
    "inputHp", "inputHpDetector", "outputLp", "mix"
  ];
  const $ = (id) => document.getElementById(id);
  const plugin = $("plugin");
  const responseSvg = $("responseGraph");
  const rtaSvg = $("rtaGraph");

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function clean(value, digits = 1) {
    const threshold = 0.5 * Math.pow(10, -digits);
    const next = Math.abs(value) < threshold ? 0 : value;
    return next.toFixed(digits);
  }

  function makeParams(mode = 9) {
    const descriptor = MODES[mode];
    return {
      mode,
      drive: mode === 9 ? 36 : 0,
      character: descriptor.defaultCharacter || 0,
      secondary: descriptor.secondaryDefault || 0,
      asym: 0,
      asymStereo: false,
      placementMode: 0,
      placement: 0,
      dynamic: 0,
      speed: 100,
      inputHp: 20,
      inputHpDetector: false,
      tone: 0,
      stages: 1,
      outputLp: 20000,
      mix: 1
    };
  }

  function cloneContext(source) {
    const result = {};
    CONTEXT_KEYS.forEach((key) => { result[key] = source[key]; });
    return result;
  }

  const state = {
    enabled: true,
    multiband: true,
    autoGain: 1,
    quality: 0,
    output: 0,
    master: makeParams(9),
    bands: Array.from({ length: 4 }, (_, index) => ({
      saturation: makeParams(9),
      bypass: false,
      trim: [0, -1.2, 0.8, 0][index]
    })),
    linked: true,
    bandCount: 4,
    phase: 0,
    selectedBand: 0,
    soloBand: -1,
    crossovers: [100, 500, 2000],
    slopes: [24, 24, 24]
  };

  let activeMenuAnchor = null;
  let rtaDrag = null;
  let lastRtaPress = null;
  let pinnedCrossover = -1;
  let crossoverFrequencyEdit = -1;

  function svgEl(name, attrs = {}, text = "") {
    const node = document.createElementNS(NS, name);
    Object.entries(attrs).forEach(([key, value]) => node.setAttribute(key, String(value)));
    if (text) node.textContent = text;
    return node;
  }

  function targetParams() {
    if (state.multiband && !state.linked) {
      return state.bands[state.selectedBand].saturation;
    }
    return state.master;
  }

  function copyContext(source, destination) {
    CONTEXT_KEYS.forEach((key) => { destination[key] = source[key]; });
  }

  function setContextValue(key, value) {
    const target = targetParams();
    target[key] = value;
    if (state.multiband && state.linked) {
      state.bands.forEach((band) => { band.saturation[key] = value; });
    }
    renderAll();
  }

  function getValue(key) {
    if (key === "output") return state.output;
    if (key === "quality") return state.quality;
    if (key === "trim") return state.bands[state.selectedBand].trim;
    return targetParams()[key];
  }

  function setValue(key, value) {
    if (key === "output") state.output = value;
    else if (key === "quality") state.quality = value;
    else if (key === "trim") state.bands[state.selectedBand].trim = value;
    else setContextValue(key, value);
    if (key === "output" || key === "quality" || key === "trim") renderAll();
  }

  function modeDescriptor() {
    return MODES[targetParams().mode];
  }

  function sineErosionFrequency(character) {
    const amount = clamp(character, 0, 1);
    if (amount <= 0.5) {
      const lower = 2 * amount;
      return 1000 * lower * lower;
    }
    return 1000 * Math.pow(10, 2 * amount - 1);
  }

  function downsampleRate(drive) {
    const normal = clamp(drive / 36, 0, 1);
    return 48000 * Math.pow(0.0125, normal);
  }

  function formatRate(value) {
    if (value >= 1000) {
      const digits = value >= 10000 ? 0 : 1;
      return clean(value / 1000, digits) + " kHz";
    }
    return clean(value, value >= 100 ? 0 : 1) + " Hz";
  }

  function formatDrive(value, mode) {
    if (mode === 22) {
      const bits = clamp(24 - Math.round(23 * clamp(value / 36, 0, 1)), 1, 24);
      return bits + " bit";
    }
    if (mode === 21) return formatRate(downsampleRate(value));
    return clean(value, 1) + " dB";
  }

  function formatCharacter(value, mode) {
    const descriptor = MODES[mode];
    if (mode === 9) return formatRate(sineErosionFrequency(value));
    const percentage = Math.round(100 * value);
    if (mode === 19) return percentage + "% / " + clean(2 + 6 * clamp(value, 0, 1), 1);
    if (mode === 16) {
      const topology = value < 0.25 ? "SINGLE" : value > 0.75 ? "WEST COAST" : "DUAL";
      return percentage + "% / " + topology;
    }
    if (mode === 23) return percentage + "% / " + Math.round(15 * clamp(value, 0, 1)) + " bit";
    return Math.round(100 * (descriptor.bipolar ? value : clamp(value, 0, 1))) + "%";
  }

  function formatValue(key) {
    const params = targetParams();
    const value = getValue(key);
    if (key === "drive") return formatDrive(value, params.mode);
    if (key === "character") return formatCharacter(value, params.mode);
    if (key === "secondary" || key === "asym" || key === "tone") return Math.round(value * 100) + "%";
    if (key === "stages") return Math.round(value) + " STAGE";
    if (key === "mix") return Math.round(value * 100) + "%";
    if (key === "placement") return Math.round(value) + "%";
    if (key === "dynamic") return (value > 0 ? "+" : "") + Math.round(value) + "%";
    if (key === "speed") return Math.round(value) + "%";
    if (key === "inputHp" || key === "outputLp") {
      if ((key === "inputHp" && value <= 20) || (key === "outputLp" && value >= 20000)) return "OFF";
      return value >= 1000 ? clean(value / 1000, value >= 10000 ? 1 : 2) + " kHz" : Math.round(value) + " Hz";
    }
    if (key === "output" || key === "trim") return clean(value, 1) + " dB";
    return String(value);
  }

  function editValue(key) {
    const value = getValue(key);
    if (key === "character" || key === "secondary" || key === "asym" || key === "tone" || key === "mix") {
      return clean(value * 100, 1);
    }
    if (key === "stages") return String(Math.round(value));
    return clean(value, 2);
  }

  function parseEditedValue(key, text) {
    if (key === "inputHp" || key === "outputLp") {
      const parsedFrequency = parseCrossoverFrequency(text);
      return Number.isFinite(parsedFrequency) ? parsedFrequency : getValue(key);
    }
    const parsed = Number.parseFloat(String(text).replace(",", "."));
    if (!Number.isFinite(parsed)) return getValue(key);
    if (key === "character" || key === "secondary" || key === "asym" || key === "tone" || key === "mix") {
      return parsed / 100;
    }
    return parsed;
  }

  function rangeFor(key) {
    const descriptor = modeDescriptor();
    if (key === "drive") return { min: 0, max: 36, step: 0.01 };
    if (key === "character") return { min: descriptor.bipolar ? -1 : 0, max: 1, step: descriptor.stepped ? 0.5 : 0.001 };
    if (key === "secondary") return { min: 0, max: 1, step: 0.001 };
    if (key === "asym" || key === "tone") return { min: -1, max: 1, step: 0.001 };
    if (key === "stages") return { min: 1, max: 8, step: 1 };
    if (key === "mix") return { min: 0, max: 1, step: 0.001 };
    if (key === "placement") return { min: -100, max: 100, step: 0.1 };
    if (key === "dynamic") return { min: -100, max: 100, step: 0.1 };
    if (key === "speed") return { min: 0, max: 100, step: 0.1 };
    if (key === "inputHp") return { min: 20, max: 2000, step: 1, logarithmic: true };
    if (key === "outputLp") return { min: 20, max: 20000, step: 1, logarithmic: true };
    if (key === "output") return { min: -24, max: 12, step: 0.01 };
    if (key === "trim") return { min: -12, max: 12, step: 0.01 };
    return { min: 0, max: 1, step: 0.001 };
  }

  function snap(value, range) {
    const clamped = clamp(value, range.min, range.max);
    const steps = Math.round((clamped - range.min) / range.step);
    return clamp(range.min + steps * range.step, range.min, range.max);
  }

  function beginEdit(field, key) {
    const input = field.querySelector(".value-input");
    if (!input || field.classList.contains("is-unavailable")) return;
    input.readOnly = false;
    input.classList.add("is-editing");
    input.value = editValue(key);
    input.focus({ preventScroll: true });
    input.select();
  }

  function finishEdit(input, key, commit) {
    if (commit) {
      const range = rangeFor(key);
      setValue(key, snap(parseEditedValue(key, input.value), range));
    }
    input.readOnly = true;
    input.classList.remove("is-editing");
    input.value = formatValue(key);
  }

  function bindRelativeField(field, key) {
    let pointer = null;
    let startY = 0;
    let startValue = 0;
    let groupStarts = null;

    field.addEventListener("pointerdown", (event) => {
      if (event.button !== 0 || event.target.closest("button") || event.target.classList.contains("is-editing")) return;
      if (field.classList.contains("is-unavailable")) return;
      event.preventDefault();
      pointer = event.pointerId;
      startY = event.clientY;
      startValue = getValue(key);
      const groupable = [
        "drive", "character", "secondary", "asym", "placement", "dynamic", "speed", "inputHp", "tone", "stages", "outputLp", "mix"
      ].includes(key);
      groupStarts = event.shiftKey && state.multiband && !state.linked && groupable
        ? state.bands.map((band) => band.saturation[key])
        : null;
      field.setPointerCapture(pointer);
    });

    field.addEventListener("pointermove", (event) => {
      if (event.pointerId !== pointer) return;
      const range = rangeFor(key);
      const fine = groupStarts ? 1 : event.shiftKey ? 0.2 : 1;
      const dragDistance = key === "placement" ? 120 : 160;
      const deltaRatio = ((event.clientY - startY) / dragDistance) * fine;
      const raw = range.logarithmic
        ? Math.exp(
          Math.log(range.min)
          + clamp(
            (Math.log(startValue) - Math.log(range.min)) / (Math.log(range.max) - Math.log(range.min)) - deltaRatio,
            0,
            1
          ) * (Math.log(range.max) - Math.log(range.min))
        )
        : startValue - deltaRatio * (range.max - range.min);
      const next = snap(raw, range);
      if (groupStarts) {
        const delta = next - startValue;
        const factor = range.logarithmic ? next / startValue : 1;
        state.bands.forEach((band, index) => {
          band.saturation[key] = snap(range.logarithmic ? groupStarts[index] * factor : groupStarts[index] + delta, range);
        });
        renderAll();
      } else {
        setValue(key, next);
      }
    });

    const end = (event) => {
      if (event.pointerId === pointer) {
        pointer = null;
        groupStarts = null;
      }
    };
    field.addEventListener("pointerup", end);
    field.addEventListener("pointercancel", end);
    field.addEventListener("click", (event) => {
      if (!event.target.closest("button")) event.preventDefault();
    });
    field.addEventListener("dblclick", (event) => {
      if (event.target.closest("button")) return;
      event.preventDefault();
      beginEdit(field, key);
    });

    const input = field.querySelector(".value-input");
    if (!input) return;
    input.addEventListener("change", () => finishEdit(input, key, true));
    input.addEventListener("blur", () => {
      if (!input.readOnly) finishEdit(input, key, true);
    });
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        finishEdit(input, key, true);
        input.blur();
      } else if (event.key === "Escape") {
        event.preventDefault();
        finishEdit(input, key, false);
        input.blur();
      }
    });
  }

  function hideMenu() {
    const menu = $("selectMenu");
    menu.hidden = true;
    menu.replaceChildren();
    menu.className = "select-menu";
    activeMenuAnchor?.classList.remove("is-picker-open");
    activeMenuAnchor = null;
    if (pinnedCrossover >= 0) {
      pinnedCrossover = -1;
      setCrossoverHover(-1);
    }
  }

  function menuButton(label, active, onSelect, splitLabel = false) {
    const button = document.createElement("button");
    button.type = "button";
    if (splitLabel) {
      const divider = label.indexOf(" ");
      const prefix = document.createElement("span");
      const value = document.createElement("strong");
      prefix.textContent = label.slice(0, divider);
      value.textContent = label.slice(divider + 1);
      button.append(prefix, value);
    } else {
      button.textContent = label;
    }
    button.classList.toggle("is-active", active);
    button.addEventListener("click", (event) => {
      event.stopPropagation();
      onSelect();
      hideMenu();
    });
    return button;
  }

  function openSimpleMenu(anchor, options, selected, onSelect, width = 0) {
    const menu = $("selectMenu");
    const shouldClose = !menu.hidden && activeMenuAnchor === anchor;
    hideMenu();
    if (shouldClose) return false;
    const isPhaseMenu = anchor.id === "phasePicker";
    options.forEach((option, index) => {
      menu.append(menuButton(option, index === selected, () => onSelect(index), isPhaseMenu));
    });
    menu.classList.toggle("is-phase-menu", isPhaseMenu);
    activeMenuAnchor = anchor;
    anchor.classList.add("is-picker-open");
    menu.hidden = false;

    const shellRect = plugin.getBoundingClientRect();
    const anchorRect = anchor.getBoundingClientRect();
    menu.style.width = Math.max(width, anchorRect.width) + "px";
    let left = anchorRect.left - shellRect.left;
    left = clamp(left, 4, shellRect.width - menu.offsetWidth - 4);
    let top = anchorRect.bottom - shellRect.top;
    if (top + menu.offsetHeight > shellRect.height - 4) {
      top = anchorRect.top - shellRect.top - menu.offsetHeight;
    }
    menu.style.left = left + "px";
    menu.style.top = Math.max(4, top) + "px";
    return true;
  }

  function miniTransferPath(modeIndex) {
    const points = [];
    for (let index = 0; index <= 24; index += 1) {
      const x = -1 + (2 * index) / 24;
      const y = representativeTransfer(x, modeIndex, {
        drive: 18,
        character: MODES[modeIndex].defaultCharacter || 0.35,
        asym: 0,
        tone: 0,
        stages: 1,
        mix: 1
      });
      points.push((index ? "L" : "M") + " " + (index / 24 * 35).toFixed(2) + " " + (7 - 5.2 * y).toFixed(2));
    }
    return points.join(" ");
  }

  function openModeMenu() {
    const menu = $("selectMenu");
    const shouldClose = !menu.hidden && activeMenuAnchor === $("algorithmCell");
    hideMenu();
    if (shouldClose) return;
    menu.classList.add("is-mode-menu");
    MODES.forEach((mode, index) => {
      const button = document.createElement("button");
      button.type = "button";
      button.classList.toggle("is-active", index === targetParams().mode);
      const number = document.createElement("span");
      number.className = "mode-menu-number";
      number.textContent = String(index + 1).padStart(2, "0");
      const name = document.createElement("span");
      name.className = "mode-menu-name";
      name.textContent = mode.name;
      const preview = svgEl("svg", { class: "mode-menu-preview", viewBox: "0 0 35 14", "aria-hidden": "true" });
      preview.append(svgEl("path", { d: miniTransferPath(index) }));
      button.append(number, name, preview);
      button.addEventListener("click", (event) => {
        event.stopPropagation();
        selectMode(index);
        hideMenu();
      });
      menu.append(button);
    });
    activeMenuAnchor = $("algorithmCell");
    activeMenuAnchor.classList.add("is-picker-open");
    menu.hidden = false;
  }

  function selectMode(index) {
    const target = targetParams();
    const descriptor = MODES[index];
    target.mode = index;
    target.character = descriptor.defaultCharacter || 0;
    target.secondary = descriptor.secondaryDefault || 0;
    if (state.multiband && state.linked) {
      state.bands.forEach((band) => copyContext(target, band.saturation));
    }
    renderAll();
  }

  function stepMode(delta) {
    const current = targetParams().mode;
    selectMode((current + delta + MODES.length) % MODES.length);
  }

  function representativeTransfer(input, mode, params) {
    const drive = 1 + 7 * clamp(params.drive / 36, 0, 1);
    const character = clamp(params.character, -1, 1);
    const unipolar = clamp(character, 0, 1);
    let value = input + params.asym * 0.12;
    const stage = () => {
      const z = value * drive;
      if (mode === 0) value = Math.tanh(z * (0.75 + 1.5 * unipolar));
      else if (mode === 1) value = clamp(z, -1 + 0.65 * unipolar, 1 - 0.25 * unipolar);
      else if (mode === 2) value = z >= 0 ? Math.tanh(z * (1 + unipolar)) : -0.55 * Math.tanh(-z * (1.8 - unipolar));
      else if (mode === 3) value = Math.tanh(z + character * 0.75) - Math.tanh(character * 0.75);
      else if (mode === 4) value = Math.abs(z) < 0.1 + 0.2 * Math.abs(character) ? 0 : Math.tanh(z);
      else if (mode === 5) value = 0.86 * Math.tanh(z) + 0.14 * Math.sin(z * (1.5 + unipolar));
      else if (mode === 6) value = Math.tanh(z) + 0.28 * character * (z * z / (1 + z * z));
      else if (mode === 7) value = Math.sin(z * (0.65 + unipolar));
      else if (mode === 8) value = Math.round(Math.tanh(z) * (3 + 20 * unipolar)) / (3 + 20 * unipolar);
      else if (mode === 9) value = Math.sin(z * (0.8 + 3 * unipolar)) * (0.7 + 0.3 * Math.sign(Math.sin(z * 8)));
      else if (mode === 10) value = Math.abs(z) < 0.08 + 0.35 * unipolar ? 0 : Math.sign(z);
      else if (mode === 11) value = Math.abs(z) < 0.04 + 0.4 * unipolar ? 0 : Math.sign(z) * Math.min(1, Math.abs(z));
      else if (mode === 12) value = (1 - unipolar) * z + unipolar * Math.abs(z);
      else if (mode === 13) value = Math.sqrt(Math.abs(Math.tanh(z))) * (z < 0 ? -1 + 2 * unipolar : 1);
      else if (mode === 14) value = Math.tanh(z + 0.2 * Math.sin(z * 2));
      else if (mode === 15) value = Math.abs(z) < 0.04 + 0.35 * unipolar ? 0 : Math.tanh(z);
      else if (mode === 16 || mode === 17) value = Math.abs(((z + 1) % 4 + 4) % 4 - 2) - 1;
      else if (mode === 18) value = Math.sin(z * (1 + 2.5 * unipolar));
      else if (mode === 19) value = Math.cos((2 + 6 * unipolar) * Math.acos(clamp(z / 2, -1, 1)));
      else if (mode === 20) value = ((z + 1) % (0.7 + 1.3 * unipolar) + (0.7 + 1.3 * unipolar)) % (0.7 + 1.3 * unipolar) - 0.5;
      else if (mode === 22) value = Math.round(clamp(z, -1, 1) * (2 + 22 * (1 - params.drive / 36))) / (2 + 22 * (1 - params.drive / 36));
      else if (mode === 23) value = Math.round(clamp(z, -1, 1) * (4 + 12 * unipolar)) / (4 + 12 * unipolar);
      else if (mode === 24) value = Math.round(z / (0.04 + 0.28 * unipolar)) * (0.04 + 0.28 * unipolar);
      else if (mode === 26) value = z > 0.18 + 0.45 * unipolar ? 1 : z < -0.18 - 0.45 * unipolar ? -1 : 0.65 * z;
      else if (mode === 27 || mode === 28) value = Math.tanh(z + character * 0.8 * Math.sin(z * 3));
      else if (mode === 29) value = Math.tanh(z) / (1 + 0.35 * unipolar * Math.abs(z));
      else value = Math.tanh(z);
    };
    const stages = clamp(Math.round(params.stages), 1, 8);
    for (let index = 0; index < stages; index += 1) stage();
    value = input * (1 - params.mix) + value * params.mix;
    value *= Math.pow(10, state.output / 20);
    return clamp(value, -1.25, 1.25);
  }

  function pathFromPoints(points) {
    return points.map((point, index) => (index ? "L " : "M ") + point[0].toFixed(2) + " " + point[1].toFixed(2)).join(" ");
  }

  function renderResponse() {
    const params = targetParams();
    const descriptor = MODES[params.mode];
    const input = [];
    const output = [];
    const left = 8;
    const right = RESPONSE_W - 8;
    const top = 10;
    const bottom = RESPONSE_H - 10;
    const centre = 0.5 * (top + bottom);
    const amplitude = 0.43 * (bottom - top);
    const dynamicRange = clamp(params.dynamic, -100, 100) / 100 * 36;
    const speed = clamp(params.speed, 0, 100) / 100;
    const attack = 0.04 + 0.92 * Math.pow(speed, 1.5);
    const release = 0.015 + 0.42 * Math.pow(speed, 1.5);
    let envelope = 0;

    for (let index = 0; index <= 220; index += 1) {
      const t = index / 220;
      const x = left + (right - left) * t;
      let source;
      if (descriptor.time) source = Math.sin(Math.PI * 6 * t);
      else source = -1 + 2 * t;
      const targetEnvelope = Math.abs(source);
      envelope += (targetEnvelope - envelope) * (targetEnvelope > envelope ? attack : release);
      const effectiveParams = dynamicRange === 0
        ? params
        : { ...params, drive: clamp(params.drive + envelope * dynamicRange, 0, 36) };
      let processed = representativeTransfer(source, params.mode, effectiveParams);
      if (descriptor.time) {
        if (params.mode === 21) processed = representativeTransfer(Math.sin(Math.PI * 6 * Math.round(t * 28) / 28), params.mode, effectiveParams);
        if (params.mode === 25) processed = clamp(processed, -0.65 - 0.25 * params.character, 0.65 + 0.25 * params.character);
        if (params.mode === 9) processed += params.secondary * 0.08 * Math.sin(251 * t);
      }
      input.push([x, centre - amplitude * source]);
      output.push([x, centre - amplitude * processed]);
    }
    $("responseInput").setAttribute("d", pathFromPoints(input));
    $("responseOutput").setAttribute("d", pathFromPoints(output));

    const grid = $("responseGrid");
    grid.replaceChildren();
    [0.25, 0.5, 0.75].forEach((ratio) => {
      grid.append(svgEl("line", {
        class: "response-grid-line" + (ratio === 0.5 ? " zero" : ""),
        x1: left,
        y1: top + (bottom - top) * ratio,
        x2: right,
        y2: top + (bottom - top) * ratio
      }));
      const verticalX = Math.round(left + (right - left) * ratio) - (ratio === 0.5 ? 1 : 0.5);
      grid.append(svgEl("line", {
        class: "response-grid-line" + (ratio === 0.5 ? " zero" : ""),
        x1: verticalX,
        y1: top,
        x2: verticalX,
        y2: bottom
      }));
    });

    const meters = $("responseMeters");
    meters.replaceChildren();
    const inputLevel = state.enabled ? 0.68 : 0;
    const outputLevel = state.enabled ? clamp(0.38 + params.drive / 72 + state.output / 48, 0, 1) : 0;
    const inputMeters = document.querySelectorAll(".level-meter.input .level-meter-fill");
    const outputMeters = document.querySelectorAll(".level-meter.output .level-meter-fill");
    inputMeters.forEach((meter, index) => {
      meter.style.height = (clamp(inputLevel + (index ? 0.02 : -0.04), 0, 1) * 100).toFixed(1) + "%";
    });
    outputMeters.forEach((meter, index) => {
      meter.style.height = (clamp(outputLevel + (index ? -0.03 : 0.01), 0, 1) * 100).toFixed(1) + "%";
    });
    [
      ["IN", 204, inputLevel, "input"],
      ["OUT", 217, outputLevel, "output"]
    ].forEach((item) => {
      meters.append(svgEl("text", { class: "meter-label", x: 18, y: item[1] + 7 }, item[0]));
      meters.append(svgEl("rect", { class: "meter-rail", x: 45, y: item[1], width: RTA_W - 63, height: 8 }));
      meters.append(svgEl("rect", { class: "meter-fill " + item[3], x: 45, y: item[1], width: (RTA_W - 63) * item[2], height: 8 }));
    });
  }

  function frequencyToX(frequency) {
    return (Math.log10(frequency) - Math.log10(MIN_FREQ)) / (Math.log10(MAX_FREQ) - Math.log10(MIN_FREQ)) * RTA_W;
  }

  function xToFrequency(x) {
    return Math.pow(10, Math.log10(MIN_FREQ) + clamp(x, 0, RTA_W) / RTA_W * (Math.log10(MAX_FREQ) - Math.log10(MIN_FREQ)));
  }

  function formatCrossoverFrequency(frequency) {
    return frequency >= 1000
      ? clean(frequency / 1000, frequency >= 10000 ? 1 : 2) + " kHz"
      : Math.round(frequency) + " Hz";
  }

  function showCrossoverTooltip(index, panelWidth) {
    const tooltip = $("rtaTooltip");
    const frequency = state.crossovers[index];
    const width = panelWidth || rtaSvg.getBoundingClientRect().width || RTA_W;
    const x = frequencyToX(frequency) / RTA_W * width;
    tooltip.dataset.crossoverIndex = String(index);
    if (crossoverFrequencyEdit !== index) tooltip.textContent = formatCrossoverFrequency(frequency);
    tooltip.style.left = clamp(x, 36, width - 36) + "px";
    tooltip.style.top = "";
    tooltip.hidden = false;
  }

  function setCrossoverHover(index, panelWidth) {
    if (index < 0 && crossoverFrequencyEdit >= 0) index = crossoverFrequencyEdit;
    if (index < 0 && pinnedCrossover >= 0) index = pinnedCrossover;
    rtaSvg.querySelectorAll(".crossover-badge-group").forEach((badge) => {
      badge.classList.toggle("is-visible", Number(badge.dataset.slopeIndex) === index);
    });
    if (index < 0) {
      $("rtaTooltip").hidden = true;
      return;
    }
    showCrossoverTooltip(index, panelWidth);
  }

  function parseCrossoverFrequency(text) {
    const normalized = String(text).trim().toLowerCase().replace(",", ".").replace(/\s+/g, "");
    const multiplier = normalized.includes("k") ? 1000 : 1;
    const parsed = Number.parseFloat(normalized);
    return Number.isFinite(parsed) ? parsed * multiplier : NaN;
  }

  function finishCrossoverFrequencyEdit(index, commit) {
    if (crossoverFrequencyEdit !== index) return;
    const tooltip = $("rtaTooltip");
    const input = tooltip.querySelector(".crossover-frequency-input");
    if (commit && input) {
      const parsed = parseCrossoverFrequency(input.value);
      if (Number.isFinite(parsed)) {
        const lower = index === 0 ? MIN_FREQ : state.crossovers[index - 1] * 1.259921;
        const upper = index === state.bandCount - 2 ? MAX_FREQ : state.crossovers[index + 1] / 1.259921;
        state.crossovers[index] = clamp(parsed, lower, upper);
      }
    }
    crossoverFrequencyEdit = -1;
    renderAll();
    setCrossoverHover(index, rtaSvg.getBoundingClientRect().width);
  }

  function beginCrossoverFrequencyEdit(index) {
    if (index < 0 || index >= state.bandCount - 1 || crossoverFrequencyEdit >= 0) return;
    hideMenu();
    crossoverFrequencyEdit = index;
    setCrossoverHover(index, rtaSvg.getBoundingClientRect().width);
    const tooltip = $("rtaTooltip");
    const input = document.createElement("input");
    input.className = "crossover-frequency-input";
    input.type = "text";
    input.inputMode = "decimal";
    input.value = String(Math.round(state.crossovers[index]));
    input.setAttribute("aria-label", "Crossover frequency in hertz");
    input.addEventListener("pointerdown", (event) => event.stopPropagation());
    input.addEventListener("click", (event) => event.stopPropagation());
    input.addEventListener("blur", () => finishCrossoverFrequencyEdit(index, true));
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        finishCrossoverFrequencyEdit(index, true);
      } else if (event.key === "Escape") {
        event.preventDefault();
        finishCrossoverFrequencyEdit(index, false);
      }
    });
    tooltip.replaceChildren(input);
    input.focus({ preventScroll: true });
    input.select();
  }

  function hideGhostCrossover() {
    $("ghostCrossover")?.classList.remove("is-visible");
  }

  function updateGhostCrossover(event, rect) {
    const ghost = $("ghostCrossover");
    const addZone = event.target.closest("[data-add-crossover]");
    if (!ghost || !addZone || state.bandCount >= 4 || rect.width <= 0) {
      hideGhostCrossover();
      return;
    }

    const band = Number(addZone.dataset.band);
    const x = (event.clientX - rect.left) / rect.width * RTA_W;
    const frequency = xToFrequency(x);
    const boundaries = [MIN_FREQ].concat(state.crossovers, [MAX_FREQ]);
    const lower = boundaries[band] * 1.259921;
    const upper = boundaries[band + 1] / 1.259921;
    if (frequency <= lower || frequency >= upper) {
      hideGhostCrossover();
      return;
    }

    ghost.setAttribute("x1", x);
    ghost.setAttribute("x2", x);
    ghost.classList.add("is-visible");
  }

  function trimToY(trim) {
    return RTA_H * 0.5 - clamp(trim, -12, 12) / 12 * (RTA_H * 0.33);
  }

  function yToTrim(y) {
    return clamp((RTA_H * 0.5 - y) / (RTA_H * 0.33) * 12, -12, 12);
  }

  function spectrumPath(offset, strength) {
    const points = [];
    for (let index = 0; index <= 130; index += 1) {
      const x = index / 130 * RTA_W;
      const shape = Math.sin(index * 0.29 + offset) + 0.52 * Math.sin(index * 0.83 + offset * 1.7) + 0.26 * Math.sin(index * 1.91);
      const envelope = 0.55 + 0.45 * Math.sin(Math.PI * index / 130);
      points.push([x, RTA_H * 0.798 - strength * envelope * (2.2 + shape)]);
    }
    return pathFromPoints(points);
  }

  function renderRta() {
    rtaSvg.replaceChildren();
    const activeCrossovers = state.crossovers.slice(0, state.bandCount - 1);
    const boundaries = [MIN_FREQ].concat(activeCrossovers, [MAX_FREQ]);

    [50, 100, 500, 1000, 2000, 5000, 10000].forEach((frequency) => {
      const x = frequencyToX(frequency);
      rtaSvg.append(svgEl("line", { class: "rta-grid", x1: x, y1: 0, x2: x, y2: RTA_H }));
      const label = frequency >= 1000 ? (frequency / 1000) + "k" : String(frequency);
      rtaSvg.append(svgEl("text", { class: "rta-label", x: x + 4, y: RTA_H - 6 }, label));
    });
    RTA_GUIDE_Y.forEach((y) => {
      rtaSvg.append(svgEl("line", { class: "rta-grid", x1: 0, y1: y, x2: RTA_W, y2: y }));
    });

    for (let band = 0; band < state.bandCount; band += 1) {
      const x1 = frequencyToX(boundaries[band]);
      const x2 = frequencyToX(boundaries[band + 1]);
      const region = svgEl("rect", {
        class: "rta-band" + (band === state.selectedBand ? " is-selected" : ""),
        x: x1,
        y: 0,
        width: Math.max(0, x2 - x1),
        height: RTA_H,
        "data-band": band
      });
      rtaSvg.append(region);
      if (state.bandCount < 4) {
        rtaSvg.append(svgEl("rect", {
          class: "crossover-add-zone",
          x: x1,
          y: 0,
          width: Math.max(0, x2 - x1),
          height: RTA_H * 0.5,
          "data-band": band,
          "data-add-crossover": ""
        }));
      }

      const soloActive = state.soloBand === band;
      const bypassActive = state.bands[band].bypass;
      const buttonX = x1 + 7;
      const soloY = RTA_GUIDE_Y[0];
      const bypassY = 52;
      rtaSvg.append(svgEl("rect", {
        class: "band-button-hitbox",
        x: buttonX - 3,
        y: soloY - 4,
        width: 24,
        height: 24,
        "data-band-solo": band
      }));
      rtaSvg.append(svgEl("rect", {
        class: "band-button" + (soloActive ? " is-active" : ""),
        x: buttonX,
        y: soloY,
        width: 18,
        height: 16,
        "data-band-solo": band
      }));
      rtaSvg.append(svgEl("text", {
        class: "band-button-text" + (soloActive ? " is-active" : ""),
        x: buttonX + 9,
        y: soloY + 8
      }, "S"));
      rtaSvg.append(svgEl("rect", {
        class: "band-button-hitbox",
        x: buttonX - 3,
        y: bypassY - 4,
        width: 24,
        height: 24,
        "data-band-bypass": band
      }));
      rtaSvg.append(svgEl("rect", {
        class: "band-button" + (bypassActive ? " is-active" : ""),
        x: buttonX,
        y: bypassY,
        width: 18,
        height: 16,
        "data-band-bypass": band
      }));
      rtaSvg.append(svgEl("text", {
        class: "band-button-text" + (bypassActive ? " is-active" : ""),
        x: buttonX + 9,
        y: bypassY + 8
      }, "B"));
    }

    rtaSvg.append(svgEl("path", { class: "rta-spectrum input", d: spectrumPath(0.3, 12) }));
    rtaSvg.append(svgEl("path", { class: "rta-spectrum output", d: spectrumPath(1.1, 15) }));

    for (let band = 0; band < state.bandCount; band += 1) {
      const x1 = frequencyToX(boundaries[band]);
      const x2 = frequencyToX(boundaries[band + 1]);
      const y = trimToY(state.bands[band].trim);
      rtaSvg.append(svgEl("line", { class: "trim-line", x1: x1 + 6, y1: y, x2: x2 - 6, y2: y }));
      rtaSvg.append(svgEl("rect", {
        class: "trim-handle",
        x: (x1 + x2) * 0.5 - 5,
        y: y - 5,
        width: 10,
        height: 10
      }));
      rtaSvg.append(svgEl("rect", {
        class: "trim-hitbox",
        x: x1,
        y: y - 17,
        width: Math.max(0, x2 - x1),
        height: 34,
        "data-trim-band": band
      }));
    }

    activeCrossovers.forEach((frequency, index) => {
      const x = frequencyToX(frequency);
      rtaSvg.append(svgEl("line", { class: "crossover-line", x1: x, y1: 0, x2: x, y2: RTA_H }));
      rtaSvg.append(svgEl("rect", {
        class: "crossover-hitbox",
        x: x - 10,
        y: 28,
        width: 20,
        height: RTA_H - 28,
        "data-crossover": index
      }));
      const badge = svgEl("g", { class: "crossover-badge-group", "data-slope-index": index });
      badge.append(svgEl("rect", { class: "crossover-badge", x: x - 32, y: 4, width: 64, height: 20 }));
      badge.append(svgEl("text", { class: "crossover-badge-text", x, y: 14 }, state.slopes[index] + " dB/oct"));
      rtaSvg.append(badge);
    });

    if (state.bandCount < 4) {
      rtaSvg.append(svgEl("line", {
        id: "ghostCrossover",
        class: "ghost-crossover",
        x1: 0,
        y1: 0,
        x2: 0,
        y2: RTA_H
      }));
    }
  }

  function renderControls() {
    const params = targetParams();
    const descriptor = MODES[params.mode];
    $("modeNumber").textContent = String(params.mode + 1).padStart(2, "0");
    $("modeName").textContent = descriptor.name;
    $("characterLabel").textContent = descriptor.character;
    $("secondaryLabel").textContent = descriptor.secondary || "DETAIL";
    $("secondaryField").classList.toggle("is-unavailable", !descriptor.secondary);

    ["drive", "character", "secondary", "asym", "placement", "dynamic", "speed", "inputHp", "tone", "stages", "outputLp", "mix", "output"].forEach((key) => {
      const input = $(key + "Value");
      if (input && !input.classList.contains("is-editing")) input.value = formatValue(key);
      const field = document.querySelector(`.value-field[data-key="${key}"]`);
      const range = rangeFor(key);
      const ratio = range.logarithmic
        ? (Math.log(getValue(key)) - Math.log(range.min)) / (Math.log(range.max) - Math.log(range.min))
        : (getValue(key) - range.min) / Math.max(1e-9, range.max - range.min);
      field?.style.setProperty("--value-ratio", String(clamp(ratio, 0, 1)));
      if (key === "stages" && field) {
        const activeStages = Math.round(getValue(key));
        field.querySelectorAll(".stage-visual > i").forEach((segment, index) => {
          segment.classList.toggle("is-active", index >= 8 - activeStages);
        });
      }
    });

    const placementIsCentered = Math.abs(params.placement) < 0.05;
    $("routeModeValue").textContent = params.placementMode === 0 ? "M/S" : "T/S";
    $("placementLabel").textContent = placementIsCentered
      ? (params.placementMode === 0 ? "CENTER" : "SUM")
      : params.placementMode === 0
        ? (params.placement < 0 ? "MID" : "SIDE")
        : (params.placement < 0 ? "TRNSNT" : "SUSTAIN");
    $("qualityValue").textContent = QUALITY[state.quality];
    $("autoGainValue").textContent = ["OFF", "REGULAR", "SMART"][state.autoGain];
    $("autoGain").classList.toggle("is-active", state.autoGain !== 0);
    $("powerValue").textContent = state.enabled ? "ON" : "OFF";
    $("power").classList.toggle("is-active", state.enabled);
    $("multibandValue").textContent = state.multiband ? "ON" : "OFF";
    $("multiband").classList.toggle("is-active", state.multiband);
    document.querySelector(".multiband-strip").classList.toggle("has-linked-active", state.multiband && state.linked);
    plugin.classList.toggle("is-expanded", state.multiband);
    plugin.classList.toggle("is-bypassed", !state.enabled);
    $("asymStereo").classList.toggle("is-active", params.asymStereo);
    $("inputHpDetector").classList.toggle(
      "is-active", params.inputHpDetector);
    $("inputHpDetector").querySelector(".route-destination").textContent =
      params.inputHpDetector ? "DYN" : "IN";
    $("linkBands").classList.toggle("is-active", state.linked);
    $("phaseValue").textContent = PHASES[state.phase];
  }

  function renderAll() {
    renderControls();
    renderResponse();
    if (state.multiband) renderRta();
  }

  function removeCrossover(index) {
    if (state.bandCount <= 2) return;
    const removedBand = index + 1;
    state.crossovers.splice(index, 1);
    state.slopes.splice(index, 1);
    state.bands.splice(removedBand, 1);
    state.bandCount -= 1;
    if (state.soloBand === removedBand) state.soloBand = -1;
    else if (state.soloBand > removedBand) state.soloBand -= 1;
    if (state.selectedBand === removedBand) state.selectedBand = index;
    else if (state.selectedBand > removedBand) state.selectedBand -= 1;
    $("rtaTooltip").hidden = true;
    renderAll();
  }

  function resetBandTrim(band) {
    state.selectedBand = band;
    state.bands[band].trim = 0;
    renderAll();
  }

  function bindUi() {
    document.querySelectorAll(".value-field[data-key]").forEach((field) => {
      bindRelativeField(field, field.dataset.key);
    });

    $("themeToggle").addEventListener("click", () => plugin.classList.toggle("is-inverted"));
    $("previousMode").addEventListener("click", () => stepMode(-1));
    $("nextMode").addEventListener("click", () => stepMode(1));
    $("algorithmPicker").addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      event.preventDefault();
      event.stopPropagation();
      openModeMenu();
    });

    $("autoGain").addEventListener("click", () => {
      state.autoGain = (state.autoGain + 1) % 3;
      renderAll();
    });
    $("routeMode").addEventListener("click", () => {
      setContextValue("placementMode", targetParams().placementMode === 0 ? 1 : 0);
    });
    $("power").addEventListener("click", () => {
      state.enabled = !state.enabled;
      renderAll();
    });
    $("multiband").addEventListener("click", () => {
      state.multiband = !state.multiband;
      hideMenu();
      renderAll();
    });
    $("asymStereo").addEventListener("click", (event) => {
      event.stopPropagation();
      setContextValue("asymStereo", !targetParams().asymStereo);
    });
    $("inputHpDetector").addEventListener("click", (event) => {
      event.stopPropagation();
      setContextValue(
        "inputHpDetector", !targetParams().inputHpDetector);
    });

    $("qualityPicker").addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      event.preventDefault();
      event.stopPropagation();
      openSimpleMenu($("qualityPicker"), QUALITY, state.quality, (index) => {
        state.quality = index;
        renderAll();
      });
    });

    $("phasePicker").addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      event.preventDefault();
      event.stopPropagation();
      openSimpleMenu($("phasePicker"), PHASES.map((phase) => "PHASE " + phase), state.phase, (index) => {
        state.phase = index;
        renderAll();
      });
    });

    $("linkBands").addEventListener("click", () => {
      if (state.linked) {
        state.bands.forEach((band) => copyContext(state.master, band.saturation));
        state.linked = false;
      } else {
        copyContext(state.bands[state.selectedBand].saturation, state.master);
        state.bands.forEach((band) => copyContext(state.master, band.saturation));
        state.linked = true;
      }
      renderAll();
    });

    rtaSvg.addEventListener("pointerdown", (event) => {
      const solo = event.target.closest("[data-band-solo]");
      if (solo) {
        event.preventDefault();
        event.stopPropagation();
        const band = Number(solo.dataset.bandSolo);
        state.selectedBand = band;
        state.soloBand = state.soloBand === band ? -1 : band;
        renderAll();
        return;
      }

      const bypass = event.target.closest("[data-band-bypass]");
      if (bypass) {
        event.preventDefault();
        event.stopPropagation();
        const band = Number(bypass.dataset.bandBypass);
        state.selectedBand = band;
        state.bands[band].bypass = !state.bands[band].bypass;
        renderAll();
        return;
      }

      const slope = event.target.closest("[data-slope-index]");
      if (slope) {
        event.preventDefault();
        event.stopPropagation();
        const index = Number(slope.dataset.slopeIndex);
        const opened = openSimpleMenu(slope, SLOPES.map((value) => value + " dB/oct"), SLOPES.indexOf(state.slopes[index]), (choice) => {
          state.slopes[index] = SLOPES[choice];
          renderAll();
        });
        pinnedCrossover = opened ? index : -1;
        setCrossoverHover(opened ? index : -1, rtaSvg.getBoundingClientRect().width);
        return;
      }

      const crossover = event.target.closest("[data-crossover]");
      if (crossover) {
        event.preventDefault();
        const index = Number(crossover.dataset.crossover);
        const now = performance.now();
        const isDoublePress = lastRtaPress
          && lastRtaPress.type === "crossover"
          && lastRtaPress.index === index
          && now - lastRtaPress.time < 360;
        lastRtaPress = { type: "crossover", index, time: now };
        if ((event.detail >= 2 || isDoublePress) && state.bandCount > 2) {
          lastRtaPress = null;
          removeCrossover(index);
          return;
        }
        setCrossoverHover(index, rtaSvg.getBoundingClientRect().width);
        rtaDrag = { type: "crossover", index, pointer: event.pointerId };
        rtaSvg.setPointerCapture(event.pointerId);
        return;
      }

      const trim = event.target.closest("[data-trim-band]");
      if (trim) {
        event.preventDefault();
        state.selectedBand = Number(trim.dataset.trimBand);
        const now = performance.now();
        const isDoublePress = lastRtaPress
          && lastRtaPress.type === "trim"
          && lastRtaPress.index === state.selectedBand
          && now - lastRtaPress.time < 360;
        lastRtaPress = { type: "trim", index: state.selectedBand, time: now };
        if (event.detail >= 2 || isDoublePress) {
          lastRtaPress = null;
          resetBandTrim(state.selectedBand);
          return;
        }
        rtaDrag = { type: "trim", index: state.selectedBand, pointer: event.pointerId };
        rtaSvg.setPointerCapture(event.pointerId);
        return;
      }

      const band = event.target.closest("[data-band]");
      if (band) {
        const bandIndex = Number(band.dataset.band);
        const addZone = event.target.closest("[data-add-crossover]");
        if (addZone && state.bandCount < 4) {
          const rect = rtaSvg.getBoundingClientRect();
          const x = (event.clientX - rect.left) / rect.width * RTA_W;
          const frequency = xToFrequency(x);
          const boundaries = [MIN_FREQ].concat(state.crossovers, [MAX_FREQ]);
          const lower = boundaries[bandIndex] * 1.259921;
          const upper = boundaries[bandIndex + 1] / 1.259921;
          if (frequency > lower && frequency < upper) {
            const source = state.bands[bandIndex];
            state.crossovers.splice(bandIndex, 0, frequency);
            state.slopes.splice(bandIndex, 0, 24);
            state.bands.splice(bandIndex + 1, 0, {
              saturation: cloneContext(source.saturation),
              bypass: false,
              trim: source.trim
            });
            if (state.soloBand > bandIndex) state.soloBand += 1;
            state.bandCount += 1;
            state.selectedBand = bandIndex + 1;
            renderAll();
            return;
          }
        }
        state.selectedBand = bandIndex;
        renderAll();
      }
    });

    rtaSvg.addEventListener("pointermove", (event) => {
      const rect = rtaSvg.getBoundingClientRect();
      const x = (event.clientX - rect.left) / rect.width * RTA_W;
      const y = (event.clientY - rect.top) / rect.height * RTA_H;
      if (rtaDrag) hideGhostCrossover();
      else updateGhostCrossover(event, rect);
      const hoveredCrossover = event.target.closest("[data-crossover], [data-slope-index]");
      if (!rtaDrag && hoveredCrossover) {
        const index = Number(hoveredCrossover.dataset.crossover ?? hoveredCrossover.dataset.slopeIndex);
        setCrossoverHover(index, rect.width);
      } else if (!rtaDrag) {
        setCrossoverHover(-1);
      }

      if (!rtaDrag || event.pointerId !== rtaDrag.pointer) return;
      if (rtaDrag.type === "trim") {
        state.bands[rtaDrag.index].trim = snap(yToTrim(y), { min: -12, max: 12, step: event.shiftKey ? 0.01 : 0.1 });
      } else {
        const index = rtaDrag.index;
        const lower = index === 0 ? MIN_FREQ : state.crossovers[index - 1] * 1.259921;
        const upper = index === state.bandCount - 2 ? MAX_FREQ : state.crossovers[index + 1] / 1.259921;
        state.crossovers[index] = clamp(xToFrequency(x), lower, upper);
      }
      const dragged = { type: rtaDrag.type, index: rtaDrag.index };
      renderAll();
      if (dragged.type === "crossover") setCrossoverHover(dragged.index, rect.width);
    });

    rtaSvg.addEventListener("pointerleave", (event) => {
      hideGhostCrossover();
      if ($("rtaTooltip").contains(event.relatedTarget)) return;
      if (!rtaDrag) setCrossoverHover(-1);
    });

    rtaSvg.addEventListener("dblclick", (event) => {
      const crossover = event.target.closest("[data-crossover]");
      if (crossover && state.bandCount > 2) {
        event.preventDefault();
        removeCrossover(Number(crossover.dataset.crossover));
        return;
      }

      const trim = event.target.closest("[data-trim-band]");
      if (trim) {
        event.preventDefault();
        resetBandTrim(Number(trim.dataset.trimBand));
      }
    });

    const endRtaDrag = (event) => {
      if (rtaDrag && event.pointerId === rtaDrag.pointer) {
        const ended = { type: rtaDrag.type, index: rtaDrag.index };
        rtaDrag = null;
        renderAll();
        if (ended.type === "crossover") {
          setCrossoverHover(ended.index, rtaSvg.getBoundingClientRect().width);
        }
      }
    };
    rtaSvg.addEventListener("pointerup", endRtaDrag);
    rtaSvg.addEventListener("pointercancel", endRtaDrag);

    $("rtaTooltip").addEventListener("pointerdown", (event) => event.stopPropagation());
    $("rtaTooltip").addEventListener("click", (event) => {
      event.stopPropagation();
      if (event.target.closest(".crossover-frequency-input")) return;
      beginCrossoverFrequencyEdit(Number($("rtaTooltip").dataset.crossoverIndex));
    });
    $("rtaTooltip").addEventListener("pointerleave", (event) => {
      if (crossoverFrequencyEdit >= 0 || pinnedCrossover >= 0 || rtaSvg.contains(event.relatedTarget)) return;
      setCrossoverHover(-1);
    });

    document.addEventListener("pointerdown", (event) => {
      if (!$("selectMenu").contains(event.target)) hideMenu();
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") hideMenu();
    });
  }

  bindUi();
  renderAll();
})();
