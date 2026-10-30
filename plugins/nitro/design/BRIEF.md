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

## MPC animation

The MPC skin moves like the OP-1 mmmf screen (`display/mmmf.svg`). Every image comes from `mpc/make_art.py`; `mpc/layout.conf` places six filmstrip meters on the plate around the bottle.

- FREQ (blue): a half dial with a needle that sweeps left to right, a lit arc behind it. Recovered from the OP-1 art: a blue line pivoting on a base dot inside five radial ticks. 33 frames on `freq`.
- RES (mint): a stack of ten bars that light from the bottom as RES rises, the last bar fades in. Recovered: the OP-1 draws ten green lines g0 to g9. 33 frames on `res`.
- DRIVE (red): same ten-bar stack in red, flared wider toward the top. Recovered: ten red lines r0 to r9. The flare is my own. 33 frames on `drive`.
- SHAPE (lilac): the real magnitude response of the current filter shape, computed from the shape table in `core/nitro_core.c`, morphing LP 24 through THRU and NOTCH to HP 24. Invented: the OP-1 only shows two fixed white glyphs, so this mapping is mine.
- Signal: `act.png` is a glow at the brass valve (blue and red lobes plus a pulse ring, balance follows the FREQ step) and `mist.png` is the violet mist at the foot of the bottle (drifts with the FREQ step). Both use the engine's `act` frame (freq step * 8 + level step) and are fully transparent at silence, so the faceplate looks the same as the JUCE one at rest.
- Touching a knob-driven meter moves that knob, they sit between the knobs and the bottle where nobody should tap.
- Not verified on a device.
