# Grid DSP

This is a new float implementation inspired by the OP-1 Grid effect. It is not bit-exact and contains no firmware code,
tables or names. I wrote it from what I learned reading the Blackfin decompile of the effect.

## What came from the reverse engineering

- Grid is a 2D wave equation on a mesh, scanned in raster order so the mesh acts like a delay line. Two time planes
  are kept and each cell is updated from its four neighbours in the other plane.
- Two scan heads run at once, one on odd rows and one on even rows, with the second half a row ahead of the first.
  Input is written a few rows ahead of the heads and the output is read at the heads.
- A window over the columns is applied when writing, updating and reading.
- One pass through the mesh takes (columns x row pairs) samples. At the default 124 x 124 that is about 168.75 ms, and
  the echoes repeat at that spacing.
- Output is dry plus wet, with dry = min(1, 2 x (1 - mix)) and the send into the mesh equal to mix.
- Block size is 64 samples and the parameter defaults are [8000, 8000, 18000, 18000] of 32767. The knob names and
  colors (X blue, Y green, Z FDB lavender, MIX red) come from the firmware screen asset.

## What this version does

- Dual mono: two meshes, one per channel, sharing scan position and controls.
- Update per cell: new = g x (0.5 x sum of the four neighbours - old), with g = window(column) x feedback squared.
- Soft ceiling on cell values (knee at 0.5, ceiling 0.7) and denormal flush on write, so it cannot blow up.
- Input is clamped to +-4 and NaN is treated as 0 for injection. Dry stays bit-exact at mix 0 from a fresh state.
- Feedback and mix are smoothed over 5 ms with an exact snap at the target.
- Edges scale by sqrt(sample rate / 44100); the core takes caller-provided memory (`grid_memory_floats`).
- X and Y resizing is click-free: a resize only happens when the first head is at a row start, at most 4 cells at a
  time, with a wet fade of about 1.5 ms around the change. The second head stalls or catches up so it stays half a row
  ahead. New cells are zeroed.

## What I inferred

- The window shape. The firmware table was not in the image I loaded, so I used a half sine.
- X and Y range. Norm maps to units floor(norm x 128), clamped 3..127, edge = 2 x units, so 6 to 254 cells. This assumes a
  raw range of 0..16383.
- Soft ceiling, input clamp and smoothing times are my choices, not facts from the firmware.
- Wet peak scaling for the animation (about 0.3 for a normal hit) is from my own tests.

## Tested (core/tests/run.sh, ASan and UBSan)

Control mapping, bit-exact dry at mix 0, impulse structure (inverted first echo, repeat spacing), feedback controls the
tail, scan matches a plain full-grid leapfrog (worst difference 3e-9), undriven mesh never grows, noise stays finite and
bounded from 22.05 kHz to 192 kHz at seven corner settings, block size 1 equals block 128 (with a resize), geometry and level
sweeps do not click, no stale cells after shrink then grow, reset clears everything, channels are independent, pass time
follows sample rate, activity meter, bad input does not poison the mesh.

## Not tested

Listening comparison against a real OP-1, Windows builds, the MPC device, AU load in a host, AAX (needs the SDK and
signing), and whether the window and X/Y range inferences match the hardware.
