# Pulse faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Knob accents are the OP-1 screen colours: blue `#4d9eff` (SHAPE), green `#00ed95` (SWEEP), white `#dfd9ff` (RATE), orange
  `#ff8a1f` (DETUNE). The envelope and level knobs use muted grey. The neon bloom and voice dots use cyan `#3fd2ff`.
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Centre: a neon glass tube bent into a pulse-width-modulated pulse train, generated with Higgsfield (`design/art/neon-b.png`,
  alternate `neon-a.png`). `compose_faceplate.py` places it on the plate and writes `art/faceplate-bg-1280x630.png`: the picture is
  700 px square at x 290, y -30, lightened onto the plate and feathered at its edges. The idea comes from the OP-1 pulse screen,
  a blue pulse train. No text, logo or person in the art.
- Left column: SHAPE above SWEEP. Right column: RATE above DETUNE. Each is a 240 x 250 cell: label (36 px), 270 degree ring
  (radius 74, stroke 6), readout (76 px), caption (12 px mono). DETUNE is bipolar: its arc grows from 12 o'clock.
- Bottom strip under the picture: ATTACK, DECAY, SUSTAIN, RELEASE, LEVEL, five 128 x 120 cells starting at x 320, y 500
  (label 22 px, ring radius 28, readout 30 px).
- Wordmark PULSE top left at (48, 24) with PULSE-WIDTH SYNTH under it and eight voice dots at (50, 86).
- Animation: the neon tube gets a cyan bloom that follows the output level (rises at once, falls over about 250 ms) and the
  voice dots light for each sounding voice. Nothing moves when the synth is silent.

Layout source: the Claude Design project "Pulse faceplate" (`Pulse.dc.html`, link in the README). It shows the layout with a
striped placeholder where the Higgsfield picture goes, because the 400 KB picture is not uploaded there. Interactions: drag a knob
vertically, 220 px for the full range, double-click for the default.

## MPC animation

The MPC skin (`mpc/`) moves like the OP-1 pulse screen (`display/pls.svg`), redrawn in my own neon style. `mpc/make_art.py` regenerates every image. The neon picture is replaced by an empty stage (`mpc/images/bg.png`) so the elements can move; the JUCE plugin still uses `design/art/faceplate-bg-1280x630.png`.

- Blue pulse train follows SHAPE: resting width from square to thin (33 frames). Element recovered from the OP-1 blue curve, the width mapping comes from the core.
- Green dashed span with two dots follows SWEEP: the dots move apart with LFO depth. The OP-1 green dashed line and dots are recovered, the travel is invented.
- White twin brackets follow RATE: the gap between them is the LFO period, tight when fast. Recovered from the white t-bars, mapping invented.
- Orange stepped bars follow DETUNE: flat at zero, steps rise for plus and drop for minus. Recovered from the pink bars, mapping invented.
- Cyan bloom on the train follows the output level (64 frame `act`, shape step x level step). Silent means nothing drawn. Loud notes add a wider halo and small sparks on the pulse tops.
- A clear `guard.png` meter on `act` covers the three small meters so stray touches do not move a knob.
