# Grid factory presets

12 presets for the Grid effect, stored in `mpc/presets.json` (one file for the MPC VST2 port and the JUCE plugin). Preset 0 is Init and equals the OP-1 factory defaults exactly. Every preset sets X, Y, Z FDB and MIX; `act` is never stored.

## Reading the table

- **Cells** is the mesh edge in cells at 44.1 kHz (X x Y). **Pass** is the time of one trip through the mesh, `1000 * (X-2) * ((Y-2)/2) / 44100` ms, which is the spacing of the repeats. Note values are at 90 BPM (1/4 = 667 ms, 1/8 = 333 ms, 1/16 = 167 ms). Pass time does not change with sample rate, because the edges scale by sqrt(rate / 44100).
- **g** is the per-pass loss factor at the window peak, `Z FDB^2`. **Send** is the wet send, equal to MIX. **Dry dB** is `20 log10(min(1, 2 * (1 - MIX)))`, so MIX up to 0.5 leaves dry at 0 dB and only the wet adds in parallel.
- **T60** is measured, not predicted: an impulse of 0.5 through the core at 44.1 kHz, 20 ms RMS windows, time until the wet envelope is last within 60 dB of its peak (dry removed). Repeats are quantized to the pass time, so these read in steps of one pass for long meshes.
- Mesh feedback and output level are tied: the wet level scales with g, so a short tail with a quiet wet is a property of the DSP and not a mistake. Presets with a very low g use a high send to compensate where it helps.

| Name | Role | Cells | Pass | g | Send | Dry dB | T60 s | Devvon source | Rationale |
|---|---|---|---|---|---|---|---|---|---|
| Init | factory default | 124 x 124 | 168.8 ms (OP-1 default) | 0.302 (Z FDB 0.5494) | 0.55 | -0.9 | 1.70 | none: OP-1 factory values | The OP-1 factory values (raw 8000, 8000, 18000, 18000), 124 by 124 cells and a 169 ms pass, kept bit for bit as the reset point. |
| Vocal Plate | vocal, lead | 68 x 68 | 49.4 ms (~50 ms pass) | 0.722 (Z FDB 0.85) | 0.28 | 0.0 | 1.80 | NA__F7m2HJViN38.md, NA__bQazPfXdqpU.md | A short plate-like bloom (about 1.8 s impulse T60, under 1 s on program material) with a pass near the 35 to 36 ms pre-delay Devvon uses on plates, mixed low (dry stays at unity) so it is felt and the lead stays clear. |
| Adlib Slap | ad-lib | 88 x 88 | 83.9 ms (~1/32) | 0.640 (Z FDB 0.8) | 0.50 | 0.0 | 2.28 | NA__s14DgJOfXr0.md | A 1/32 slap with a few quick repeats for ad-libs; Devvon uses slap delay for width, which Grid cannot give because it is dual mono, so this only adds a short repeat. |
| Throw Aux 1/4 | vocal throw (aux) | 254 x 236 | 668.6 ms (1/4) | 0.360 (Z FDB 0.6) | 1.00 | off | 8.06 | NA__s14DgJOfXr0.md, NA__P7DOp0tZx8I.md | Quarter-note repeats at 100 percent wet for an aux send in parallel; Devvon describes a throw delay as like a fourth delay, which fits the 1/4 pass and the in-and-out use. Wet-only output is still quiet at this g, so plan on fader make-up on the aux. |
| Drum Bus Room | drum bus | 30 x 30 | 8.9 ms (no note value) | 0.490 (Z FDB 0.7) | 0.22 | 0.0 | 0.16 | none: derived from DSP | A 9 ms square mesh that smears hits into a tight, dense room (a 0.2 s tail) at a low send so the bus gets body you feel and do not hear; it is a small room, not compression. |
| Kick Box | kick | 254 x 6 | 11.4 ms (~87 Hz repeat) | 0.640 (Z FDB 0.8) | 0.30 | 0.0 | 0.36 | none: derived from DSP | A long thin 254 by 6 mesh acts like a short tube, adding a boxy ring around 87 Hz at a light send; nothing from Devvon covers kick space. |
| Snare Chamber | snare | 60 x 46 | 28.9 ms (~29 ms) | 0.722 (Z FDB 0.85) | 0.40 | 0.0 | 1.06 | NA__F7m2HJViN38.md | A rectangular 29 ms mesh with about a 1 s impulse tail gives the snare a chamber-style sense of space, which Devvon uses on snares and instruments. |
| 808 Sub A | 808 | 42 x 42 | 18.1 ms (~55 Hz repeat) | 0.810 (Z FDB 0.9) | 0.30 | 0.0 | 1.00 | none: derived from DSP | The pass is 18.1 ms, so the repeat rate sits near 55 Hz (A1), close to an A-tuned 808; retune X and Y for other keys. Devvon has nothing on 808 space. |
| Keys Halo | keys | 172 x 174 | 331.5 ms (1/8) | 0.490 (Z FDB 0.7) | 0.50 | 0.0 | 5.66 | NA__s14DgJOfXr0.md, NA__F7m2HJViN38.md | A sparse 1/8 bloom (long impulse T60 but low in level) that blends in-plugin like Devvon's plate and hall mix without washing the part. |
| Pad Cloud | pad | 152 x 76 | 125.9 ms (~1/32 dotted) | 0.680 (Z FDB 0.8246) | 0.42 | 0.0 | 4.04 | NA__F7m2HJViN38.md | A wide 2:1 mesh at 1/32 dotted with high feedback builds a dense 4 s cloud, longer than Devvon's 2.8 s hall, for pads that should sit behind everything. |
| Hat Scatter | hats | 254 x 80 | 222.9 ms (1/8 triplet) | 0.490 (Z FDB 0.7) | 0.50 | 0.0 | 3.80 | NA__F7m2HJViN38.md | A thin 254 by 80 mesh at 1/8 triplet scatters hats into ricochets that fade over a few seconds at a moderate send; Devvon puts a chamber on hi-hats to make digital sound more live and acoustic. |
| Metal Drone | FX | 24 x 26 | 6.0 ms (~167 Hz repeat) | 0.990 (Z FDB 0.995) | 0.38 | 0.0 | 1.70 | none: derived from DSP | A 6 ms mesh at near-unity feedback rings like struck metal for about 1.7 s; a happy-accident texture, not a Devvon technique. |

## Devvon, honestly

Devvon has no direct guidance on Grid or on a 2D waveguide mesh. What carries over is general: plate, chamber and hall blending and keeping reverb short on a plate (0.7 to 0.93 s, pre-delay around 35 to 36 ms), a chamber on snares and hats, slap and sync'd note-value delays, parallel auxes, and a light touch so effects are felt more than heard. Presets whose source reads "none: derived from DSP" come from how the mesh behaves, not from anything he said. Grid is dual mono (two meshes sharing scan position and controls), so no preset claims stereo widening.

## How the numbers were made

`grid_convert.py` (`/private/tmp/claude-501/-Users-thomasphillips-bombest-audio-mpc-re/adf080b6-ecad-470d-83b7-9e8c85ecc18b/scratchpad/presets/grid_convert.py`) turns real-world targets into 0..1 values using the exact inverse of `grid_core.c`:

- X and Y: `cells = 2 * clamp(floor(norm * 128), 3, 127)`. The inverse is `(u + 0.5) / 128` (the bucket center, as in `grid_norm_from_cells`), never `u / 128`, which sits on a bucket edge. Pass-time targets are met by searching all even (X, Y) pairs, because cells are quantized and the aspect X/Y also shapes the sound.
- Z FDB: `norm = sqrt(g)`. MIX: `norm = send`.
- g was chosen by running the real core, since decay depends on the window, the neighbour coupling and the soft ceiling and not just on g.
- Each preset was forward-mapped back to cells, pass time, g and send, and checked against its target. All 12 presets were also run on a full-scale noise burst at 44.1, 48 and 96 kHz and stayed finite and bounded.

Not verified: listening on a host or the MPC device, and AU/VST2 loading.
