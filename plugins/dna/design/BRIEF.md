# DNA faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents: blue `#2f8bff` (MUTATE, ATK, DEC), green `#1de9a0` (BITS, SUS, REL), orange `#ff8a1f` (SUB, GRIT, LVL).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: a neon DNA helix, generated with Higgsfield (`design/art/helix-b.png`, 630 x 630 at x 325), fading to the plate.
  `helix-a.png` is the alternate. The helix is an original image with no real person or logo in it.
- Left column: MUTATE above BITS. Right column: SUB above GRIT. Each is a 240 x 250 cell: label (36 px), 270 degree ring
  (radius 74, stroke 6), readout (76 px), caption (12 px mono). MUTATE is bipolar: its ring fills from the top centre.
- Bottom strip, 80 x 100 mini knobs either side of the helix: ATK and DEC on the left, SUS, REL and LVL on the right.
- Wordmark DNA top left at (48, 24) with GENOME OSCILLATOR under it.
- Motion: the helix glows with the output peak, with one soft blob per sounding voice, flashes on each new note and drifts
  slowly. The snapshot tool holds a glow level for stills (`DNASnapshot out.png 1280 0.8 6`).

Source of truth for the layout is the Claude Design project "DNA faceplate" (`DNA.dc.html`):
https://claude.ai/design/p/c0c8b6a9-7bb0-430b-a035-8efdb8dc7145
Interactions: drag a knob vertically, 220 px for the full range; double-click returns it to its default.

## MPC animation

The MPC skin (`mpc/`) moves like the OP-1 screen: each knob drives its own element and the signal level drives a pulse around the helix. `mpc/make_art.py` regenerates every image in `mpc/images/`.

The OP-1 screen art for dna is `id.svg`, a set of uncoloured tile glyphs, and the decompile has no drawing code, so the knob-to-element mapping is my own design. I drew it from the core instead: the grid is the real 128-bit genome (same seed and per-gene thresholds as `core/dna_core.c`).

- MUTATE (blue): the 128 genes as a 16 x 8 grid. Genes flip one at a time as the knob moves, with a white flash while one is mid-change.
- BITS (green): a 128-tick bar that fills to the cycle length (2 to 128 genes).
- SUB (orange): the two-cycle table. The first cycle stays put, the second is negated and rotated as SUB rises.
- GRIT (orange): a row of bit-error cells that light up as GRIT rises.
- Signal: `act` draws a bloom, ripples and sparks around the helix (blue left, green right). Level sets size and brightness, the MUTATE step changes ripple spacing and spark positions. Silent means nothing is drawn.

`images/bg.png` is an MPC-only copy of the faceplate with the empty wells. `design/art/faceplate-bg-1280x630.png` is untouched. I could not see it on the device.
