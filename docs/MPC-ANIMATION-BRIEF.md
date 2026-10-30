# Brief: OP-1 style animation on the MPC skin

Each plugin's MPC skin (`plugins/<id>/mpc/`) is mostly a static faceplate with knobs. The user wants it to move the way
the OP-1 and OP-1 Field screen moves: the engine's own graphic reacts when you turn a Q-Link (each of the four coloured
elements, blue, green, white, orange, travels or morphs with its knob) and the whole graphic reacts to the signal level
(pulse, glow, wobble). Subtle, in the plugin's own look, not a copy of the OP-1 pixels.

## What you own

Only `plugins/<id>/mpc/`: `layout.conf`, `images/`, a `make_art.py` that regenerates every image, and `README` notes in
`design/BRIEF.md` (append an "MPC animation" section). Do not edit `engine.c`, the core, the JUCE plugin, root CMake,
`common/` or `tools/`. No git, no installs on the device, no signing.

## What already exists

`tools/add_mpc_activity.py` already gave every port a generic version: an engine-driven parameter `act` (frame =
lead knob step * 8 + signal level step, 64 frames) shown by one `meter` line in `layout.conf` using `images/act.png`
(a plain glow and ring). Replace that generic look with something that belongs to this engine. Do not run the script
again, it would overwrite your art lines. Keep the `act` parameter and its meaning (the engine writes it; the frame
number is `knob_step * 8 + level_step`, both 0..7), but you may redraw `images/act.png` however you like and move or
resize the meter.

## The OP-1 reference

The firmware screen art is in `~/bombest-audio/op1f-re/firmware/op1/op1_246/content/display/<name>.svg`. Find the
one for your engine (the colour dots and shapes are what the OP-1 animates; ids and colours tell you which element
belongs to which knob: blue `#698eff`, green `#00ed95`, white, orange/red). The decompile for the engine is at
`~/bombest-audio/op1f-re/work/decomp/dsp/<Codename>/` and `~/bombest-audio/op1-docs/` has manuals. Look at how the
elements are laid out (tracks, lines, grids) and decide how each one should move with its knob: a dot along a track,
a shape growing, a line bending, a grid stretching. If a mapping is not recoverable, design a sensible one and say so
in your report. No SVG renderer is installed: parse the SVG geometry yourself with Python or look at it in the
built-in browser (`mcp__Claude_Browser__navigate` to a `file://` URL, then a screenshot). Never copy the SVG artwork
into the repo, redraw it in your own style with Pillow.

## Mechanisms the MPC gives you

- **Knob-driven element**: `meter cx= cy= w= h= key=<knob key> strip=images/<name>.png frames=N` is a display-only
  filmstrip that shows the frame matching that parameter's value. It moves while you turn the Q-Link or the knob, no
  engine work needed. Use 33 frames for a smooth travel. One meter per knob, each drawing only its own element on a
  transparent background, so the four layers add up on top of the faceplate.
- **Signal-driven element**: the `act` meter (see above), 64 frames. Make the level axis do the pulse/glow and let the
  knob axis do a small change in the same element, or draw only the level part and ignore the knob axis.
- Strips are vertical stacks of frames, RGBA with transparent edges. A strip may not be taller than 12288 px in
  total (frame height * frames). Keep frames small (an element, not the whole screen), for example 160x160 or
  420x120. The `layout.conf` coordinates are page coordinates: the faceplate art line is at `y=86`, so a faceplate
  pixel (x, y) is at (x, y + 86). Meters sit on top of the background art and the touch areas of anything under them
  are swallowed, so do not place a meter over a knob. The four knobs are at the corners (about x 180/1100, y 316/556);
  the middle of the plate is free for the animated graphic.
- A meter is display only. Touching it sends a value but the engine ignores `act`, and the knob keys are real
  parameters, so touching a knob-driven meter would move the knob: put knob-driven meters where nobody will touch
  them, or accept that touching them changes that knob. Prefer small, non-intrusive placements.
- The faceplate background `design/art/faceplate-bg-1280x630.png` already has the hero art. If your animated
  elements need an empty stage, you may make a new background image for the MPC only (`mpc/images/bg.png`, 1280x630,
  referenced by the `art file=` line); keep `design/art/faceplate-bg-1280x630.png` untouched because the JUCE plugin
  uses it.
- Everything must look good at rest (signal at zero) and subtle in motion. The wrapper refreshes about every 100 ms,
  so design for gentle motion, not fast flicker.

## Validate (do all of it)

```bash
cd ~/bombest-audio/mpc-re/mpc-vst-plugins
tools/test_port.sh ~/bombest-audio/op1-fx/plugins/<id>/mpc/vst.json      # must print PASSED
```

Run `tools/build_port.sh ~/bombest-audio/op1-fx/plugins/<id>/mpc/vst.json aarch64` once at the end, but only one agent
may use Docker at a time. Take the lock first and always release it:

```bash
until mkdir /tmp/op1fx-docker.lock 2>/dev/null; do sleep 15; done
trap 'rmdir /tmp/op1fx-docker.lock' EXIT
tools/build_port.sh .../vst.json aarch64
```

Read its warnings (overlaps, edge, strip limits) and fix them. The built skin images are in
`plugins/<id>/mpc/build/skin/*/Plugin Skins/`; look at the page renders and at a few composites you make yourself:
paste your strips over the faceplate at 3 knob positions and 3 signal levels (silent, medium, loud) and view the
PNGs. Remove `plugins/<id>/mpc/build/` when you are done.

House rules: never write an em dash or an en dash anywhere, hyphens only. First person singular in docs, plain and
short.

## Report

Say which OP-1 screen element you mapped to which knob (and what was recovered versus invented), the files, the real
output of `test_port.sh` and the build warnings, the composites you looked at, and what you could not verify (you
cannot see it on the device).
