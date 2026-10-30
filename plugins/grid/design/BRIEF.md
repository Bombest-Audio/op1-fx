# Grid faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`. Same as Punch.
- Knob colors come from the OP-1 Grid screen asset: X blue `#698eff`, Y green `#00ed95`, Z FDB lavender `#adadff`,
  MIX red `#ff3a5d`. The lattice lines on the OP-1 screen are `#382649`.
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: an isometric 2:1 wireframe lattice with a ripple in the middle, generated with Higgsfield
  (`design/art/grid-a.png`, alternate `grid-b.png`). The OP-1 screen for Grid is an isometric lattice, so that is the idea.
  The art was scaled to 0.633, centered at (640, 330), screen-blended onto the plate and feathered 70 px at the sides, which
  gives `design/art/faceplate-bg-1280x630.png`. No text is baked into the art.
- Left column: X above Y. Right column: Z FDB above MIX. Each knob is a 240 x 250 cell:
  label (36 px), 270 degree ring (radius 74, stroke 6.2), readout (76 px), caption (12 px mono).
- Wordmark GRID top left at (48, 24) with WAVEGUIDE MESH under it, and a PASS readout (one trip through the mesh, in ms)
  top right.
- The editor animates the lattice with the wet signal: a soft glow at the ripple origin (640, 318) and 2:1 ellipse ripples
  that leave it on each new hit, alpha 0.35 at most.

Source of truth for the layout is the Claude Design project "Grid faceplate" (`Grid.dc.html`). In that file the lattice is a
CSS stand-in, since I did not upload the PNG there; the plugin and the MPC port use the Higgsfield art.
