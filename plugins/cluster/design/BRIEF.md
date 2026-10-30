# Cluster faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: mint `#2ef0a0` (OSCS), violet `#a77bff` (FEEDBACK), amber `#ffb347` (SPREAD), coral `#ff4d5e` (WANDER).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a bundle of eight glowing sine strands that start as one point on the left and fan out to the right. That is the
  idea of the engine: eight sines that start in phase and drift apart. Generated with Higgsfield (job
  `02bd3eab-171e-45e2-995a-7488e0cf04a0`, one of the four allowed generations is used for the final art; source file
  `design/art/strands-a.png`, 1024 x 1024). The final `faceplate-bg-1280x630.png` is that image scaled to 0.62 and placed
  on the plate at x 323, y 8. The small dots that were baked into the generated image are painted out, and the editor draws
  its own eight dots live.
- Left column: OSCS above FEEDBACK. Right column: SPREAD above WANDER. Each knob is a 240 x 250 cell: label (36 px),
  270 degree ring (radius 74, stroke 6), readout (76 px), caption (12 px mono).
- Eight live dots in a 4 x 2 grid near (640, 325), 22 px apart. Each lights with its oscillator's strength. A faint glow
  at the root of the strands (336, 320) follows the output level, at most 22 percent alpha.
- Bottom strip, y 545: six small controls ATK, DEC, SUS, REL, LEVEL, BEND at a 100 px pitch starting at x 340.
- Wordmark CLUSTER top left at (48, 24) with SELF-FEEDBACK SINE CLUSTER under it.

Source of truth for the first layout is the Claude Design project https://claude.ai/design/p/23533524-9bf0-406a-9ce4-d5fb16f22eab
(it holds placeholder art and the four knobs only; the bottom strip and live dots were added in the JUCE editor).
The JUCE editor in `plugin/PluginEditor.cpp` is the current layout. Interactions: drag a knob vertically, 220 px for the
full range, double-click returns to the default. `design/editor-preview.png` is the editor at rest and
`design/editor-active.png` is the editor with the dots lit.
