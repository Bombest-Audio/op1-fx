# Phase faceplate brief

- Size: 1280 x 630 design space, resizable at fixed aspect. On the MPC the art sits at y = 86.
- Palette: plate 07080c, ink e9ece8, dim 6c7370, line 23262a.
- Knob accents: A 698eff, B 00ed95, R dfd9ff, W ff3a5d.
- Fonts: Barlow Condensed for labels, JetBrains Mono for values.
- Layout: four big knobs at the corners (A, B left; R, W right), a scope panel in the middle drawing the live wave from the knob values, six small knobs under it (attack, decay, sustain, release, level, bend), eight voice pips.
- Art: two Higgsfield generations (ribbons-1, ribbons-2) of glowing blue and mint ribbons on near-black. ribbons-1 is composited into `design/art/faceplate-bg-1280x630.png`.
- Claude Design project: https://claude.ai/design/p/19b258b5-5817-45ff-a665-1437ddbd6e30
- Animation: a marker sweeps the scope, glow follows output level, pips light with sounding voices, all on a 60 Hz timer.

## MPC animation

The MPC skin (`mpc/`) has an 800x190 stage in the middle of the plate, drawn by `mpc/make_art.py` into `mpc/images/`. The shapes come from the OP-1 phase screen (`display/pd.svg`): blue and green half cycles with end dots, a red burst wave, a white dotted line with a head dot. Each knob drives its own filmstrip layer (33 frames):

- A (blue): the positive half cycle bends, the peak dot slides along it. Recovered: blue half cycle with end dots. Invented: the bend curve.
- B (green): same on the negative half cycle.
- W (red): four cycles of a sine, each fading towards its end by W. Recovered: red burst wave with a head dot. Invented: the fade law.
- R (white): the dotted line fills up to a head dot. Recovered: white dotted line with a dot at its start. Invented: head dot travel.
- Signal level (`act`, 64 frames): panel border, axes, ghosts and glow behind the waves come up with the output level. Only the level axis is used, the knob axis is ignored.

Not checked on the device, only in composites over the faceplate.
