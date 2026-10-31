# Punch faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: violet `#a77bff` (FREQ), red `#ff4d5e` (PUNCH, POWER), mint `#2ef0a0` (ROUNDS).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a boxer, a Detroit tribute generated with Higgsfield (`design/art/on-1.png`), the lead arm thrust out toward the viewer in the pose of the Joe Louis Fist monument, cropped to 620 x 630 at x 330, bottom fading to the plate.
  Alternates kept in `design/art/`: `champ-1`, `champ-3`, and `classic-1`, `classic-2` (an early-1900s champion look).
  The boxer is a stylized illustration, a Detroit tribute. The lead arm follows the pose of the Joe Louis Fist monument. It is not a photographic likeness.
- Left column: FREQ above PUNCH. Right column: ROUNDS above POWER. Each knob is a 240 x 250 cell:
  label (32 px), 270 degree ring (radius 74, stroke 6), readout (66 px), caption (12 px mono).
- Wordmark PUNCH top left at (48, 30) with TRANSIENT STACK under it.

Source of truth for the layout is the Claude Design project "Punch faceplate" (`Punch.dc.html`). Interactions: drag a knob
vertically, 220 px for the full range.

## MPC animation

The MPC skin moves like the OP-1 screen for this engine (`display/mllp.svg`, the boxer). `mpc/make_art.py` regenerates every strip in `mpc/images/` with Pillow, all in this plugin's own look.
The Punch generator also uses NumPy to cut the glove layer that moves over the faceplate.

- FREQ (violet): a needle sweeping a 270 degree tick dial. Recovered from the OP-1: its blue needle on green ticks is the FREQ dial.
- PUNCH (red): a burst of rays that grows with the knob. The OP-1 has four rays by the glove; the exact mapping is not recoverable, so I designed it.
- ROUNDS (mint): a ladder of bars that fills up. The OP-1 has a ladder of ten green bars; I use 24 cells because the knob has 24 rounds.
- POWER (red): shock arcs that light up and thicken going outward. Designed, the OP-1 shows the glove art here.
- Signal (`act`): the forward glove grows up to 3.5 percent on an output transient, then returns to guard. I use a slow level baseline to distinguish a hit from a sustained signal. The knob axis repeats the same eight poses. The glove only grows, so it always covers its resting self and the motion leaves no double image.

Each knob strip is a display-only `meter` on the knob's own key, 33 frames, placed beside its knob. The desktop faceplate background is unchanged.
