# Sampler faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents follow the OP-1 knob order: blue `#698eff` (START), green `#2ef0a0` (END), white `#e9ece8` (TUNE), orange `#ff9a3d`
  (LOOP). The envelope knobs and ROOT are small and use blue.
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a glowing blue glass waveform running top to bottom, mirrored about a thin centre axis. The idea is the OP-1 sampler
  screen, which shows the sample as a waveform with its start and end. Generated with Higgsfield (`gpt_image_2_5`, one
  generation, `design/art/wave-1.png`, 752 x 1344), scaled to 352 x 630 and screen-blended onto the plate with
  `ImageChops.lighter`, centered at x 640. Original abstract graphic, no text, logo or person.
- The plugin draws over the art: a faint trace of the loaded sample down the middle, the parts outside START and END dimmed,
  a blue marker at START and a green one at END (mapped to y 20 to 610), and a thin playhead that follows the newest voice.
  Output level adds a very faint blue glow behind the trace (alpha up to about 0.16, falls over about 200 ms).
- Left column: START above END. Right column: TUNE above LOOP. Each big knob is a 240 x 250 cell: label (36 px), 270 degree
  ring (radius 74, stroke 6), readout (76 px), caption (12 px mono).
- Inner columns: small 150 x 130 knobs. Left x 335: ATK y 110, DEC y 250, LEVEL y 390. Right x 795: SUS y 110, REL y 250, ROOT y 390.
  Ring radius 26, label 20 px, readout 26 px.
- Wordmark SAMPLER top left at (48, 24) with ONE SAMPLE, EIGHT VOICES under it.
- Sample bar at (490, 566), 300 x 40: the sample name. Click it for a menu (load a file, or one of three built-in samples), or
  drop a WAV or AIFF anywhere on the window. A missing saved file shows as NOT FOUND in orange.
- Interactions: drag a knob vertically, 220 px for the full range, double-click for default.

Source of truth for the layout is the Claude Design project "Sampler faceplate" (`Sampler.dc.html`,
https://claude.ai/design/p/d91a3fe5-792a-432e-b62b-b1956739f828). The art there is a striped placeholder, since the project
cannot take the PNG from here. Claude Design's standalone MCP server stops working on 2026-12-14, so the layout is also
described here.
