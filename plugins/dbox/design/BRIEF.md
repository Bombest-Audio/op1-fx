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

## MPC animation

The MPC skin (`mpc/`) moves like the OP-1 screen. I read `display/dbox.svg` from the OP-1 firmware and redrew the elements in my own style with Pillow (`mpc/make_art.py` regenerates every strip). Each element is a transparent filmstrip `meter` (33 frames) bound to its knob, so it moves while the Q-Link or knob turns. Nothing is copied from the SVG.

- PITCH, blue: ring with a tick that turns -135 to +135 degrees and an arc from 12 o'clock. Recovered from `p1ring` and `p1line`.
- WAVE, green: waveform glyph morphing sine, triangle, square, with three selector pips. The OP-1 shows waveform glyphs and a green selector box; the morph is mine.
- DECAY, white: ring with a marker and bars inside that get a longer tail. Ring and tick recovered from `w1ring` and `w1line`; the bars are mine.
- MODE, orange: two rings, 8 detents, a tick that snaps in 45 degree steps. Rings and ticks recovered from `modering`, `redring`, `funkline`, `filtline`; the 8 step mapping is mine.
- OSC2 tab: MIX 2 (blue ring), WAVE 2 (teal glyph), DECAY 2 (grey ring and bars), FILTER (orange low-pass curve with a cutoff marker). The ring and glyph set follows the dim second oscillator row of the screen; the filter curve is mine.
- Signal level, all tabs: the engine-driven `act` parameter (frame = PITCH step * 8 + level step) draws an orange glow on the drum head and elliptical ripples around it. Level sets glow and ripple count, PITCH tightens the ripples and moves the tint from blue to orange. At zero level nothing is drawn, so the rest state is the plain hero art. This part is mine, the OP-1 has no level animation.

The elements sit in the free area under the cube, centred at page y 500, x 400, 560, 720, 880 (150 x 150 each). Touching one moves that knob. I could not check any of it on the device.
