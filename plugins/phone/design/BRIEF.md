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

## MPC animation

The MPC skin (`mpc/`) now moves like the OP-1 Phone screen (`display/ptch.svg`). `mpc/make_art.py` regenerates every strip in `mpc/images/`. Each knob owns one element, drawn on a transparent filmstrip over the faceplate, so the Q-Link moves its own picture with no engine work.

- TONE (blue): a dial track with three ticks and a dot that rides it, the travelled part lit. `tone.png`, 33 frames, left of the handset. Recovered from the OP-1 (blue pot and dial), the dot travel is my design.
- PHONIC (green): a ring of N dots around the mic capsule, N = 1..15 (cycles per fragment). `phonic.png`, 15 frames. The OP-1 colours the mic green; the dot-count mapping is invented.
- BAUD (lilac white): a pulse train that goes from 1 to 8 cycles as BAUD rises. `baud.png`, 31 frames, right of the handset. The OP-1 colours the chip white; the waveform mapping is invented.
- TELEMATIC (red): ten ticks beside the speaker that wake up one by one and grow. `noise.png`, 33 frames. The tick positions are scaled from the OP-1 noise cluster; the wake order is invented.
- Signal level: three `act` meters (`act_spk.png`, `act_mic.png`, `act_chip.png`, 64 frames each) put a glow and dashed arcs on the speaker (red, arcs go out), the mic (green, arcs come in) and the chip (blue halo). Only the level axis is used, so the knob axis repeats the same picture. Silent shows nothing, so rest is the plain art.

I could not see it on the device. Checked with composites over the faceplate at three knob positions and three levels, and `test_port.sh` and `skin_check.py` both pass.
