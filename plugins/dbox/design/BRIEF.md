# DBox faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents follow the OP-1 knob order: blue `#698eff` (PITCH), green `#2ef0a0` (WAVE), white `#e9ece8` (DECAY), orange
  `#ff9a3d` (MODE). The extra and envelope knobs are small and muted.
- Type: Barlow Condensed for labels and readouts.
- Center: a matte black cube with a drum head on top, blue and orange rim light, a drumstick resting on it. The name is a
  drum box, so the box is literal. Generated with Higgsfield (`gpt_image_2_5`, one generation, `design/art/drumbox-1.png`),
  cropped to 460 x 450, scaled by 0.62 and screen-blended (`ImageChops.lighter`) onto the plate at x 640, y 4. Original
  graphic, no text, logo or person.
- Under the cube: a 6 x 4 grid of pads at x 488, y 285 (44 px pads, 8 px gap), the same shape as the key grid on the OP-1
  screen. Pad k is drum key k, key 0 top left.
- Big knobs 240 x 250: PITCH (60,110), WAVE (60,370), DECAY (980,110), MODE (980,370). Small knobs 150 x 130: 2 MIX, 2 WAVE,
  2 DECAY at x 335, y 100, 220, 340; FILTER, LEVEL, BEND at x 795, same rows; ATK, DEC, SUS, REL on the bottom row at y 495,
  x 335, 495, 655, 815.
- Wordmark DBOX top left at (48, 24) with 24 KEY DRUM KIT under it.
- Animation: a pad lights orange when its key is struck and falls over about 230 ms. A faint orange glow on the drum head
  follows output level (alpha up to 0.2, falls over about 200 ms).

Claude Design project "DBox faceplate" (`DBox.dc.html`): https://claude.ai/design/p/1732cc5c-0332-41aa-a68e-5409347f7b43
The art is a striped placeholder there and only the four big knobs and the pad grid are drawn, since the project cannot take
the PNG from here. `design/editor-preview.png` is the real render. Claude Design's standalone MCP server stops working on
2026-12-14, so the layout is also described here. Interactions: drag a knob vertically, 220 px for the full range.
