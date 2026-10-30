# Phone faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents follow the OP-1 screen: blue `#6b8cff` (TONE), mint `#2ef0a0` (PHONIC), lilac white `#cfc9ff` (BAUD), red `#ff3d5a`
  (TELEMATIC).
- Type: Barlow Condensed for labels and readouts, JetBrains Mono for the small captions.
- Center: an exploded telephone handset, generated with Higgsfield (model gpt_image_2_5, one generation, `design/art/handset-a.png`,
  1024 x 1024), scaled to 630 x 630 and placed at x 325 on the plate. The background is lifted by (4, 5, 5) so the art blends
  into `#07080c`. The OP-1 screen for Phone is an exploded handset with a chip, a mic, a speaker and noise, which is where the
  idea comes from. The art is an original illustration with no text, logo or person in it.
- Left column: TONE above PHONIC. Right column: BAUD above TELEMATIC. Each knob is a 240 x 250 cell: label (36 px),
  270 degree ring (radius 74, stroke 6.2), readout (76 px), caption (12 px mono).
- Wordmark PHONE top left at (48, 24) with CYCLE LOOPER under it.

## Animation

Subtle and driven by the signal, drawn as vector overlays on top of the art in `PluginEditor.cpp`, so nothing is baked in:

- The green mic capsule glows with the input level, with three arcs travelling in toward it. It rises at once and falls over
  about 100 ms.
- The red speaker capsule glows with the same level, a little later and scaled with TELEMATIC, with three arcs travelling out.
- The chip glows blue for a moment each time playback moves to a new fragment.
- With no signal nothing is drawn, so a silent plugin shows the plain art.

Overlay centres in design space: speaker (544, 114), mic (749, 450), chip (728, 237). `anim-rest.png` and `anim-active.png`
are renders from the Snapshot tool (`PhoneSnapshot out.png 1280 0.9 0.8` for the lit one).

Source of truth for the layout is the Claude Design project "Phone faceplate" (`Phone.dc.html`). In that project the art is a
striped placeholder, the art itself lives here. Interactions: drag a knob vertically, 220 px for the full range.
