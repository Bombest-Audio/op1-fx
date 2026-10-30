# Delay faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Knob colors follow the OP-1 Delay screen: blue `#698eff` (TIME, the ring color on the screen), green `#00ed95` (SPEED),
  lavender white `#dfd9ff` (FDB), red `#ff3a5d` (INPUT).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions. The plugin draws them with the
  system condensed fallback at 0.82 horizontal scale, as Punch does.
- Center: a reel-to-reel tape echo machine with concentric ripple rings behind the reels, generated with Higgsfield
  (`design/art/reel-b.png`, 1024 x 1024), scaled to 630 x 630, placed at x 325 with soft edges on a flat `#07080c` plate and
  saved as `faceplate-bg-1280x630.png`. The alternate is `reel-a.png`. The idea comes from the OP-1 screen for Delay, which
  is a field of expanding rings. It is an original machine, no brand, no text.
- Left column: TIME above SPEED. Right column: FDB above INPUT. Each knob is a 240 x 250 cell: label (36 px), 270 degree ring
  (radius 74, stroke 6.2), readout (76 px), caption (12 px mono). Wordmark DELAY top left at (48, 24) with TAPE ECHO under it.
- The TIME readout is the real delay in ms (or seconds above 1 s), so it also moves with SPEED. The SPEED readout is the
  tape speed in percent after the 8 s ceiling.
- Animation (restrained): the core reports the peak echo level. A soft blue glow at the ripple center (640, 200) follows it,
  and each new echo sends one thin blue ring outward, slower when the tape is slow. Idle, nothing moves.

Source of truth for the layout is the Claude Design project "Delay faceplate" (`Delay.dc.html`), where the art is a
striped placeholder because the image is not uploaded there: https://claude.ai/design/p/64d6c1fd-0599-4af5-a841-2283bef18cae
Claude Design's standalone MCP server stops working on 2026-12-14, so the layout is also described here.

`editor-preview.png` is the editor rendered with echoes showing, `editor-rest.png` is the idle state. Both come from the
`DelaySnapshot` dev target.

## MPC animation

The MPC skin moves like the OP-1 Delay screen, which is a field of five nested blue rings. `mpc/make_art.py` regenerates
every strip in `mpc/images/`. Five layers sit over the faceplate, centred on the ripple at the middle of the machine:

- TIME (blue, `rings.png`, 33 frames): the five rings open from tight to wide. Ring radii and centre offsets are measured
  from the OP-1 `delay.svg`. Which parameter the OP-1 ties to the ring spread is not recoverable, so I mapped it to TIME.
- FDB (lavender, `fdb.png`, 33 frames): arcs trail around each ring, longer with more feedback. Invented.
- SPEED (green, `speed.png`, 33 frames): a dot orbits the middle ring, one lap over the knob range. Invented.
- INPUT (red, `input.png`, 33 frames): a dot slides along a short track into the centre, like the OP-1 red dot on its line.
- Level (`act.png`, 64 frames, engine driven): a soft blue glow that rises with the echo level and swells slightly with TIME.

Touching the knob-driven layers moves that knob, so they sit in the middle of the plate where nobody reaches for them.
At rest (signal zero) only the rings and the dots show. Not seen on the device.
