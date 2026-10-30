# Grid

A new take on the OP-1 Grid effect, as a plugin: a 2D waveguide mesh that you feed audio and read back, with four
controls and one shared DSP core.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+) | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (armv7, aarch64) | `mpc/`, built with mpc-vst-plugins |

## Controls

Names and defaults come from the OP-1 (factory values 8000, 8000, 18000, 18000 of 32767).

| Control | What it does |
|---|---|
| X | Mesh width, 6 to 254 cells. |
| Y | Mesh height, 6 to 254 cells. X and Y set the time of one pass, so they set the echo spacing. |
| Z FDB | Feedback per pass. Stable up to the top. |
| MIX | How much of the input is sent into the mesh. Dry ducks above the midpoint. At 0 the audio passes through bit for bit. |

## DSP

`core/grid_core.c` is plain C99 with no allocation (the caller provides the mesh memory). It is written from scratch as
float DSP and is not a bit-exact port. See `docs/DSP.md`.

## Build

```sh
cmake -S . -B build/grid -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=grid
cmake --build build/grid
core/tests/run.sh
```

MPC VST2 (needs Docker):

```sh
../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

## Design

Layout is in the Claude Design project "Grid faceplate" and described in `design/BRIEF.md`. The lattice art is generated
with Higgsfield and lives in `design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and
the MPC skin.
