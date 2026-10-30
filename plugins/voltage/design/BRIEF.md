# Voltage faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents follow the OP-1 knob colors: blue `#698eff` (XOR), green `#00ed95` (FEEDBACK), white `#e9ece8` (FILTER), orange `#ff9a3a` (DETUNE).
- Type: Barlow Condensed for labels and readouts, a small mono face for captions.
- Center: a vintage analog volt meter with two copper terminals and a blue arc jumping between them. It is the OP-1 screen's
  "volts" idea. The art was generated with Higgsfield (`design/art/meter-a.png`, `meter-b.png` are the raw outputs) and
  composed into `design/art/faceplate-bg-1280x630.png`, with the bottom fading to the plate. No text is baked in. It is an
  original image, not a likeness of any real device brand.
- Left column: XOR above FEEDBACK. Right column: FILTER above DETUNE. Each knob is a 240 x 250 cell: label (36 px),
  270 degree ring (radius 74, stroke 6), readout (76 px), caption (12 px).
- Along the bottom: five small controls for A, D, S, R and LEVEL, 110 x 120 cells.
- Wordmark VOLTAGE top left at (48, 24) with XOR OSCILLATOR under it.
- Animation: the arc between the terminals (about x 500 to 780, y 122) glows with the output level and jitters every third
  frame at 60 Hz. It is off when nothing plays.

Source of truth for the layout is the Claude Design project: https://claude.ai/design/p/934df742-2a34-4293-9486-17c38e524f02
Interactions: drag a knob vertically, 220 px for the full range, double-click for the default.
