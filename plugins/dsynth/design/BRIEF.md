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
