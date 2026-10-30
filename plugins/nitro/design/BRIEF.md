# Nitro faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: blue `#698eff` (FREQ), mint `#00ed95` (RES), lilac `#dfd9ff` (SHAPE), red `#ff3a5d` (DRIVE).
- Type: Barlow Condensed for labels and readouts, a small mono face for the captions.
- Center: a chrome nitrous bottle with a brass valve and a braided hose, generated with Higgsfield
  (`design/art/bottle-a.png`), composited onto the plate as `design/art/faceplate-bg-1280x630.png`. The art has no text in it.
  `bottle-b.png` is the alternate. 2 of the 4 allowed generations were used. There is no person in the art.
- Left column: FREQ above RES. Right column: SHAPE above DRIVE. Each knob is a 240 x 250 cell:
  label (36 px), 270 degree ring (radius 74, stroke 6), readout (76 px, 44 px for the SHAPE name), caption (12 px).
- Wordmark NITRO top left at (48, 24) with DRIVE FILTER under it.
- Animation: a red-pink glow on the valve at (610, 108) and a violet mist at the foot of the bottle (circle, radius 170, at
  (610, 540)). Both follow the input level. They rise at once and fall by 0.95 per frame at 60 Hz. A slow idle drift keeps the
  art from looking frozen. Alpha stays under 0.4.

Source of truth for the layout is the Claude Design project: https://claude.ai/design/p/a152d2df-f55d-4950-a6b0-7811c5efe7f7
Interactions: drag a knob vertically, 220 px for the full range. Double click resets to the default.
Claude Design's standalone MCP server stops working on 2026-12-14, so the layout is also described here.
