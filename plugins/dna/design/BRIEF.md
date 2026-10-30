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
