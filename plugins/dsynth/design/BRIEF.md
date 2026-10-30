# DSynth design brief

Editor: 1280 x 630. Dark faceplate, one big drum pad in the middle, four big knobs at the sides, five small ones.

## Palette

| Role | Hex |
|---|---|
| Background | `#07080c` |
| Ink | `#e9ece8` |
| Muted | `#6c7370` |
| Track | `#23262a` |
| MOD (blue) | `#698eff` |
| WAVE (green) | `#2ef0a0` |
| DECAY (white) | `#e9ece8` |
| FILTER (orange) | `#ff9a3d` |

Fonts: Barlow Condensed for labels and values, JetBrains Mono for small captions.

## Layout (design pixels)

- Pad: centre (640, 314), radius 165. It ripples and glows when a note plays, driven by the audio activity.
- MOD top left, WAVE bottom left, DECAY top right, FILTER bottom right. Each is a 240 x 250 cell.
- ATK, DEC, SUS in a column centred at x 390. REL and LEVEL in a column centred at x 890. Each is a 150 x 130 cell.
- Title DSYNTH top left, subtitle TWO OSCILLATORS, ONE HIT.

## Art

`design/art/pad-1.png` and `pad-2.png` are Higgsfield generations of the drum pad (2 of the 4 allowed). The faceplate
background `faceplate-bg-1280x630.png` is the pad composited on the dark ground. The MPC port draws its own knobs on it.

## Claude Design

Project: https://claude.ai/design/p/0a8aa4cd-ac90-4bc9-b6e9-5a6c7c44604d

It holds the layout with a striped placeholder where the pad goes. I did not run the render and verify loop on it.

## Preview

`design/editor-preview.png` is a real render of the JUCE editor, made with the `DSynthSnapshot` target.

## MPC animation

`mpc/make_art.py` regenerates every image in `mpc/images/`. Reference: the OP-1 `dsynth.svg` screen, redrawn in my own style.

- A row of four small graphics sits above the pad, each a 33 frame filmstrip on its own knob: MOD (blue) is a ring pair whose inner ring grows toward the outer ring, WAVE (green) is a waveform that morphs sine, triangle, saw, square with a 4 dot selector, DECAY (white) is an exponential tail that lengthens, FILTER (orange) is a response curve with a cutoff dot stepping through the 8 positions.
- `images/act.png` (64 frames, 180x180, centred on the pad) is the signal pulse: cyan rings push outward and brighten with level, and ring spacing widens with the lead knob (MOD).
- Recovered from the SVG: the big ring with an inner ring (modering/redring), the column of four dots by the oscillator (wave selector), the two oscillator rings and level boxes, the colour roles. Invented: every motion mapping, the decay curve and the filter curve. The OP-1 decompile for this engine was empty of screen logic, so none of it could be confirmed.
- Not verified on the device.
