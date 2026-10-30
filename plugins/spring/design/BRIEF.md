# Spring faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Knob colors follow the OP-1 encoder colors: blue `#698eff` (TONE), green `#2ef0a0` (TURNS), lavender `#cdb8ff` (DAMP),
  orange `#ff8a4d` (LEVEL).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: two steel coils in an open reverb-tank chassis, generated with Higgsfield (`design/art/spring-b.png`, 1024 x 1024,
  prompt asked for no text, logos or people). `design/art/spring-a.png` is the other take from the same call. The faceplate
  is built by scaling the art to 680 px wide at x 300, feathering its edges and screen-blending it onto the plate, so the
  art's own black becomes the plate color exactly (`design/art/faceplate-bg-1280x630.png`).
- Left column: TONE above TURNS. Right column: DAMP above LEVEL. Each knob is a 240 x 250 cell:
  label (36 px), 270 degree ring (radius 74, stroke 6), readout (76 px), caption (12 px mono).
- Wordmark SPRING top left at (48, 24) with DISPERSIVE TANK under it.
- Idea: the OP-1 screen for this effect is only the words TURNS and DAMP, so the picture comes from the name. A spring tank.

## Animation

The core reports the peak tank output since the last read; the editor reads it on a 60 Hz timer. The value goes through a
square root, rises at once and falls at 0.95 per frame (about 330 ms to 1/e). A soft mint glow, at most 34% alpha, runs from
the left end of each coil to the right in about 0.8 s (faster when the tank is excited), and a fainter one runs back the
other way, like a wave reflecting off the far end. With no signal nothing moves.
`design/anim-rest.png` and `design/anim-pulse.png` are frames from the snapshot tool; `design/editor-preview.png` is the rest frame.

Source of truth for the layout is the Claude Design project "Spring faceplate" (`Spring.dc.html`), where the art is a labeled
placeholder because the project holds no images. Interactions: drag a knob vertically, 220 px for the full range.

## MPC animation

The OP-1 screen for this effect (`rymd.svg`) is four coloured words plus a red dashed column with a dot at each end. The
firmware moves them, but the decompile (`Rymd`) holds no display code, so I only recovered which colour belongs to which
knob: blue TONE, green TURNS, lavender DAMP, red/orange LEVEL (the column with two dots). The motions are my own design.

The MPC skin (`mpc/`, all art from `mpc/make_art.py`) has five meters, drawn in my own style:
- TONE (blue): a low-pass curve, the corner slides right as the knob opens.
- TURNS (green): a coil that gains turns from 1.5 to 8.
- DAMP (lavender): an echo comb, the tail shortens as damp rises (higher damp is a faster fade).
- LEVEL (orange): the OP-1 dashed column, the two dots pull apart with the knob.
- `act` (signal): mint glow along both coils, longer and brighter with level, with a fainter reflected wave and a few
  sparks. The tone step tints it slightly towards blue. Nothing shows at zero signal.

The four knob meters sit in a row below the tank (page y 622). Touching one moves its knob. Not checked on a device.
