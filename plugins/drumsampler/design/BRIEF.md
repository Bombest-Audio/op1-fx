# DrumSampler faceplate brief

Claude Design project: https://claude.ai/design/p/1ee3a2e6-b219-4733-89d0-ac24613620f4?file=DrumSampler+Faceplate.dc.html
That page is a layout mock with a striped placeholder where the art goes. I did not run the render and screenshot loop on it, so treat it as unverified. The real editor is the JUCE render (`DrumSamplerSnapshot`).

## Direction
Dark plate (#07080c), condensed caps type, one accent per control: blue pitch, green in, white out, orange mode. A vinyl record cut into wedge slices sits behind the pads and glows with the playing slot.

## Art
One Higgsfield generation (`gpt_image_2_5`, job 817fd22a-b36f-4f25-8d8b-09ecaa630067, 1 of 4 allowed): `design/art/record-1.png`. It came out with 18 wedges instead of 24, so the slices are mood, not a pad map. `design/compose_faceplate.py` crops it, dims it and composes `design/art/faceplate-bg-1280x630.png`, which is embedded as BinaryData.

## Layout (1280x630)
Four big knobs on the sides (PITCH, IN, OUT, MODE), waveform with draggable in/out markers on top, six small knobs (ATK, DEC, SUS, REL, OUTPUT, SLOT LVL), then a 6x4 pad grid. Click a pad to select and audition it. Drag WAV files onto a pad to load them.

## MPC animation
The MPC skin (DRUM tab) now moves like the OP-1 drum screen. Everything is drawn by `mpc/make_art.py` in my own style, nothing is copied from the OP-1 SVG (`display/drum2.svg`).
- Waveform (act, 64 frames): the OP-1 teal sample outline. The SLOT step picks one of eight drum-like shapes, the signal level brightens, glows and swells it. The shapes are mine.
- START (green): the loop-in dot slides along a track under the waveform. The OP-1 colour, dot, ring and dashed bar are recovered, the motion is the obvious one.
- END (white): the loop-out dot slides along a track above the waveform. Same notes.
- PITCH (blue): a dot on a 12 step semitone ladder. The OP-1 pitch glyph is a static icon, so the ladder is invented.
- MODE (orange): gate, one-shot, loop glyphs in 3 frames. Recovered from the OP-1 play-mode group, redrawn.
- `mpc/images/bg.png` is the faceplate dimmed to 70 percent for legibility. The JUCE art is untouched.
