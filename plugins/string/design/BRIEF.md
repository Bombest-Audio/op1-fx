# String faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: blue `#5aa0ff` (PICK), mint `#2ef0a0` (DAMP), white `#e9ece8` (STIFF), orange `#ff9a3d` (DECAY), amber `#ffb347`
  (voice lights and bridge glow). Envelope knobs use a gray `#8a928e`.
- Type: a condensed sans for labels and readouts, small spaced caps for the captions.
- Center: six steel strings fanning up from a brass bridge saddle, generated with Higgsfield (`design/art/strings-b.png`),
  screen-blended onto the plate, 620 x 630 at x 330, feathered at the sides and top. Black space under the bridge holds the
  voice lights and the envelope strip.
- Left column: PICK above DAMP. Right column: STIFF above DECAY. Each is a 240 x 250 cell at (60,110), (60,370), (980,110),
  (980,370): label (36 px), 270 degree ring (radius 74, stroke 6), readout (76 px), caption (12 px).
- Bottom strip: five small knobs (ATTACK, DECAY, SUSTAIN, RELEASE, LEVEL), each a 118 x 110 cell at x = 345 + i * 118, y = 505.
- Eight small lights under the bridge at y 458, one per voice, lit by that voice's level.
- Wordmark STRING top left at (48, 24) with PLUCKED STRING under it.
- Animation: the strings panel is redrawn in 3 px strips, each shifted sideways by a standing wave (zero at the bridge and
  at the far end, widest in the middle). Strength follows the output level, up to 7 px, at a fixed 9 Hz look.

Claude Design project: https://claude.ai/design/p/c11e39e0-2ef5-4c6b-a8bd-64f6965a5709 (`String.dc.html`). Two known gaps in
that file: the art panel is a striped placeholder because the PNG could not be uploaded, and the subtitle sits slightly into
the wordmark. The plugin editor uses the positions above, not the dc.html overlap.

Interactions: drag a knob vertically, 220 px for the full range, double-click for the default.

MPC skin: the five envelope knobs sit in one row at y 560 (r 26) because the plugin area is 628 px tall, so they are
higher than in the desktop window.
