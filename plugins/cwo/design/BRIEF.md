# Cwo faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: blue `#698eff` (FREQ), violet `#a77bff` (DELAY), red `#ff4d5e` (FDBACK), mint `#2ef0a0` (SIDEBAND). These are the house
  palette, picked to read on the plate. I did not sample them from the OP-1 screen.
- Type: the faceplate design used Barlow Condensed. The plugin draws with the default JUCE sans squeezed to 82% width, so the labels are
  close to it but not the same face, and it has not been compared side by side.
- Center: a Holstein cow, generated with Higgsfield (`design/art/cow-c.png`, no halo, flat black backdrop), because the OP-1 screen for
  this effect is a cow. Scaled to 636 px at x 322, y -14, with a thin mint floor line under the hooves at y 528.
  Alternates kept in `design/art/`: `cow-a` and `cow-b` have a halo baked in, which can't be animated.
  The cow is an original render. It is not the OP-1 artwork and not a likeness of any real animal or person.
- Halo: 48 short ticks in violet, mint, red and blue, centre (506, 214), radius 150 to 190, drawn live behind the cow. It turns with the
  shifter (direction from SIDEBAND, speed from FREQ, capped at a third of a turn per second) and brightens with the level of the shifted
  echoes. The cow's eye gets a small cyan glow on the same level. Nothing else moves.
- Alpha: `faceplate-bg-1280x630.png` is opaque on the cow and the floor line and transparent elsewhere. Under the transparent pixels the RGB
  holds the plate color and a dim static halo, for hosts that ignore alpha. Rebuild with `python3 -I design/make_faceplate.py`.
- Left column: FREQ above DELAY. Right column: FDBACK above SIDEBAND. Each knob is a 240 x 250 cell: label (36 px), 270 degree ring
  (radius 74, stroke 6.2), readout (76 px), caption (12 px).
- Wordmark CWO top left at (48, 24) with FREQUENCY SHIFT DELAY under it.

The layout was designed in Claude Design: https://claude.ai/design/p/96aff8c8-6222-4097-9f8b-19492ae4455b. The art and the live halo
are mine to wire in, so the plugin source (`plugin/PluginEditor.cpp`) is the source of truth for what is on screen.
Interactions: drag a knob vertically, 220 px for the full range, double-click to reset.

Files: `editor-preview.png` (rest), `anim-rest.png` (no signal) and `anim-signal.png` (halo at full level, quarter turn offset) are
renders from the CwoSnapshot tool.
