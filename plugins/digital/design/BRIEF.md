# Digital faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents follow the OP-1 knob order: blue `#698eff` (FEEDBACK), green `#2ef0a0` (RATIO), white `#e9ece8` (DETUNE), orange
  `#ff9a3d` (CRUNCH). The envelope knobs are small and use blue.
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a thin violet laser line running from lower left to upper right, a blue dot with fading echoes on it, three tiny
  green, white and red dots beside the dot, and two clouds of pixel-grid and bit-flip fragments. The OP-1 digital screen
  draws a line, a few dots and glitch squares, which is where the idea comes from. Generated with Higgsfield (one
  generation kept, `design/art/line-1.png`; a second, `line-2.png`, was not used). `design/compose_faceplate.py` scales it
  to 700 x 700, lighten-blends it onto the plate with a feathered edge and writes `design/art/faceplate-bg-1280x630.png`.
  The art is an original abstract graphic with no text, logo or person.
  In the 1280 x 630 plate the line runs from (320, 432) to (990, 172) and the blue dot sits at (640, 333).
- Left column: FEEDBACK above RATIO. Right column: DETUNE above CRUNCH. Each big knob is a 240 x 250 cell: label (36 px),
  270 degree ring (radius 74, stroke 6), readout (76 px), caption (12 px mono).
- Inner columns: small 150 x 130 knobs. Left x 335: ATK y 110, DEC y 250, LEVEL y 390. Right x 795: SUS y 110, REL y 250.
  Ring radius 26, label 20 px, readout 26 px.
- Wordmark DIGITAL top left at (48, 30) with FEEDBACK SINE, SYNC, XOR under it.
- Animation: output level lights a faint blue glow behind the dot and a thin violet glow along the line (alpha up to about
  0.3 and 0.16, falls over about 200 ms). A new note sends a brighter violet spot along the line from lower left to upper
  right in about 250 ms.

Interactions: drag a knob vertically, 220 px for the full range, double-click for default. DETUNE is the one bipolar knob,
-100 to 100; above 0 it also turns hard sync on.

Claude Design project "Digital faceplate" (`Digital.dc.html`, https://claude.ai/design/p/71db94cd-4331-49cb-989e-f66e887bf7a9). The art box there is a striped placeholder, since the project cannot take the PNG from here; the knob rings are static. Claude Design's standalone MCP server stops working on
2026-12-14, so the layout is also described here.

## MPC animation

The MPC skin (`mpc/`) animates the OP-1 digital screen (firmware `display/t10.svg`: one violet line and a stack of blue, green, white and red dots). I redraw it in my own style with `mpc/make_art.py` on an empty stage (`mpc/images/bg.png`, a dim static line, so the shared faceplate art is untouched). Each knob drives one filmstrip layer (33 frames) over the DIGITAL tab:

- FEEDBACK (blue): the three blue echoes spread back along the line behind the head dot.
- RATIO (green): the green dot travels up the line with a lit segment behind it.
- DETUNE (white): the white dot slides along a rail above the line; the centre tick is zero.
- CRUNCH (orange): glitch squares appear around the line, up to 30.
- Signal level (`act`, 64 frames split in two strips): the violet line glows and widens, a blue glow swells at the dot, violet beads appear on the line. The knob axis of `act` is unused.

From the SVG I recovered the geometry, the colours and the stacked dots. Which knob moves which element, and how, is my own mapping. Touching a knob layer moves that knob, since the layers sit over the middle of the plate. Not verified on the device.
