# DrumSampler faceplate brief

Claude Design project: https://claude.ai/design/p/1ee3a2e6-b219-4733-89d0-ac24613620f4?file=DrumSampler+Faceplate.dc.html
That page is a layout mock with a striped placeholder where the art goes. I did not run the render and screenshot loop on it, so treat it as unverified. The real editor is the JUCE render (`DrumSamplerSnapshot`).

## Direction
Dark plate (#07080c), condensed caps type, one accent per control: blue pitch, green in, white out, orange mode. A vinyl record cut into wedge slices sits behind the pads and glows with the playing slot.

## Art
One Higgsfield generation (`gpt_image_2_5`, job 817fd22a-b36f-4f25-8d8b-09ecaa630067, 1 of 4 allowed): `design/art/record-1.png`. It came out with 18 wedges instead of 24, so the slices are mood, not a pad map. `design/compose_faceplate.py` crops it, dims it and composes `design/art/faceplate-bg-1280x630.png`, which is embedded as BinaryData.

## Layout (1280x630)
Four big knobs on the sides (PITCH, IN, OUT, MODE), waveform with draggable in/out markers on top, six small knobs (ATK, DEC, SUS, REL, OUTPUT, SLOT LVL), then a 6x4 pad grid. Click a pad to select and audition it. Drag WAV files onto a pad to load them.
