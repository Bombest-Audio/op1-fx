# Fm faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents follow the OP-1 knob order: blue `#698eff` (RATIO), green `#2ef0a0` (INDEX), white `#e9ece8` (FEEDBACK), orange
  `#ff9a3d` (LAYER). The envelope knobs are small and use blue.
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a stack of four glowing isometric glass cubes, one per FM operator, joined by thin lines. The OP-1 FM screen draws
  its operators as cubes, which is where the idea comes from. Generated with Higgsfield (`gpt_image_2_5`, one generation,
  `design/art/cubes-1.png`), cropped to 424 x 815, scaled to 328 x 630 and screen-blended onto the plate by
  `ImageChops.lighter`, centered at x 640. The art is an original abstract graphic with no text, logo or person.
  The cube centers are at x 640, y 66, 206, 362 and 532 (top to bottom).
- Left column: RATIO above INDEX. Right column: FEEDBACK above LAYER. Each big knob is a 240 x 250 cell: label (36 px), 270
  degree ring (radius 74, stroke 6), readout (76 px), caption (12 px mono).
- Inner columns: small 150 x 130 knobs. Left x 335: ATK y 110, DEC y 250, LEVEL y 390. Right x 795: SUS y 110, REL y 250.
  Ring radius 26, label 20 px, readout 26 px.
- Wordmark FM top left at (48, 30) with FOUR OPERATORS under it.
- Animation: output level lights a faint blue glow behind all four cubes (alpha up to about 0.12, falls over about 200 ms).
  A new note sends a brighter pulse (alpha up to about 0.3) down the cubes from top (modulator) to bottom (carrier) in about 250 ms.

Source of truth for the layout is the Claude Design project "Fm faceplate" (`Fm.dc.html`,
https://claude.ai/design/p/328f4ec1-26b2-4d28-af8d-cc01523aba8f). The cube art is a striped placeholder there, since the
project cannot take the PNG from here. Interactions: drag a knob vertically, 220 px for the full range, double-click for default.
Claude Design's standalone MCP server stops working on 2026-12-14, so the layout is also described here.

## MPC animation

The MPC skin moves like the OP-1 FM screen. The firmware art (`display/fm.svg`) is four isometric wireframe cubes stepped
down a diagonal (47.6 x 23.8 px steps, outline `#698EFF`, hidden edges `#1D2040`), plus eight small glyph groups in the four
knob colours. I redrew the cascade at 1.9x in my own style (`mpc/make_art.py`, Pillow, no SVG artwork copied). The MPC
`bg.png` is the plate with a dim cube skeleton; the cubes baked into the shared faceplate are not used there.

- RATIO (blue, 33 frames): a dot travels down the cascade through the cube bodies, cubes it reaches light their outline.
- INDEX (green, 33 frames): the two modulator cubes (1 and 3) fill from the bottom like glass.
- FEEDBACK (white, 33 frames): an isometric orbit around each modulator sweeps open, with a head dot at its end.
- LAYER (orange, 33 frames): the second pair (cubes 3 and 4) fades and glows in.
- Signal (`act`, 64 frames, two meters, top and bottom half): all cubes brighten, the output end most, links wake up. The knob
  axis is not used.

What I recovered: the cube cascade, its geometry and the blue/green/white/red colour order. What I invented: every
knob-to-element mapping, since the decompile has no screen code and the SVG does not say which group each knob moves. The
ENV page shows the static skeleton only (the LEVEL knob sits too close to the signal meters).
