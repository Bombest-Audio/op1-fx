# Punch faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: violet `#a77bff` (FREQ), red `#ff4d5e` (PUNCH, POWER), mint `#2ef0a0` (ROUNDS).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a boxer, generated with Higgsfield (`design/art/champ-2.png`), cropped to 620 x 630 at x 330, bottom fading to the plate.
  Alternates kept in `design/art/`: `champ-1`, `champ-3`, and `classic-1`, `classic-2` (an early-1900s champion look).
  The boxer is an original character. It is not a likeness of a real person.
- Left column: FREQ above PUNCH. Right column: ROUNDS above POWER. Each knob is a 240 x 250 cell:
  label (32 px), 270 degree ring (radius 74, stroke 6), readout (66 px), caption (12 px mono).
- Wordmark PUNCH top left at (48, 30) with TRANSIENT STACK under it.

Source of truth for the layout is the Claude Design project "Punch faceplate" (`Punch.dc.html`). Interactions: drag a knob
vertically, 220 px for the full range.
