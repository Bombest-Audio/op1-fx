# Porting brief: one OP-1 effect as a plugin

Each effect in this repo is a new take on an OP-1 effect: original float DSP, a four-knob faceplate, JUCE plugin
(VST3, AU, CLAP, AAX, Standalone) and an MPC VST2 port. `plugins/punch/` is the finished reference. Copy its shape.

## What you own

One folder, `plugins/<id>/`. Touch nothing outside it. Do not run git commands, push, install anything on the MPC,
sign anything, or edit the root CMake. The repo owner integrates, validates on Windows and on the device, and pushes.

```
plugins/<id>/
  CMakeLists.txt             one op1fx_add_plugin(...) call (see plugins/punch/CMakeLists.txt)
  core/<id>_core.h/.c        portable C99 DSP, no allocation, real-time safe, only libm
  core/tests/test_core.c     behavioral tests, run with core/tests/run.sh (clang, ASan+UBSan)
  plugin/PluginProcessor.*   JUCE APVTS, four params, stereo and mono, state via XML
  plugin/PluginEditor.*      1280x630 design space, resizable fixed aspect, background from BinaryData
  plugin/Snapshot.cpp        dev tool that renders the editor to a PNG
  design/art/faceplate-bg-1280x630.png    the faceplate background (exact file name, the CMake recipe needs it)
  design/BRIEF.md            palette, type, layout, where the art came from
  mpc/{engine.c,params.json,vst.json,layout.conf}   MPC VST2 port
  docs/DSP.md                what the DSP does and what it is derived from
  README.md
```

## The DSP

- The firmware decompile is at `~/bombest-audio/op1f-re/work/decomp/dsp/<Codename>/` (Blackfin fixed point). Read it to learn
  the structure: stage order, filter topology, delay lengths, how the four knobs act. The defaults are in
  `~/bombest-audio/op1f-re/firmware/op1/op1_246/content/op1_factory.db`, table `fx_types`, `default_params` (0..32767, first
  four are the knobs). Other material: `~/bombest-audio/op1-docs/`. `find` over `/` is slow, search inside those folders.
- Write original float code. Never copy decompiled code, constants tables or variable names into the repo. Say plainly in
  `docs/DSP.md` that it is a new implementation inspired by the OP-1 effect, not bit-exact, and which facts came from the RE.
- If there is no decompile for your effect, design it from what the effect is known to do on the OP-1 and say so.
- Control names: take them from what the effect does and from any labels you can find in the firmware screen assets under
  `~/bombest-audio/op1f-re/firmware`. Keep the four knobs, keep the factory defaults as the plugin defaults.
- Rules that reviewers caught before: comments must match the code, interpolations must be continuous (smooth parameter
  changes, no clicks), denormal protection, level and headroom re-checked, delay or reverb memory sized from the sample rate,
  `punch_reset`-style reset that clears all state, mono and stereo both work, sample rates 22050 to 192000 work, block
  size 1 works. Tests must check behavior (impulse and step response, frequency response, level, stability, no NaN, bit-exact
  dry at the bypass setting if there is one), not just that it runs.

## Build and validate (macOS, your own build dir)

```bash
cd ~/bombest-audio/op1-fx
cmake -S . -B build/<id> -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=<id> \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE \
  -DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=$HOME/bombest-audio/op1-fx/.deps/clap-juce-extensions
cmake --build build/<id> --parallel 4
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 8 \
  build/<id>/plugins/<id>/<Name>_artefacts/Release/VST3/<Name>.vst3
clap-validator validate build/<id>/plugins/<id>/<Name>_artefacts/Release/CLAP/<Name>.clap
```

Use only your own `build/<id>` directory. Binaries must be universal (arm64 + x86_64) with `minos 11.0` (check with
`vtool -show-build`). Render the editor with the Snapshot tool and look at the PNG before you call the UI done.
The editor should animate subtly with the signal the way `plugins/punch/plugin/PluginEditor.cpp` does (the core reports a
signal-activity value, the editor reads it on a 60 Hz timer): pick something that fits your effect and keep it restrained.

## MPC port

Toolchain: `~/bombest-audio/mpc-re/mpc-vst-plugins` (read `docs/PORTING.md`, `wrapper/engine.h`, `tools/test_port.sh`,
`tools/build_port.sh`, and `plugins/punch/mpc/` as the worked example). The target is an MPC Live III (aarch64) and the
armv7 build too. The faceplate background is the same 1280x630 PNG, four knobs, `qlinks`. Run the offline host test
(`test_port.sh`) and build both `.so` files with `build_port.sh <vst.json> all` (Docker). Skin limits: filmstrip at most
16384 px, layout y = skin y + 86. Do not run `install.sh` or touch the device.

## Design (required)

Design the faceplate with **Claude Design** (the `mcp__claude-design__*` tools: create a project, write the `.dc.html`,
render a preview) and make the main art with **Higgsfield** (`mcp__d0341341-0e03-4cf9-b80e-f862947802e5__generate_image`,
then `jobs_wait`). Limit Higgsfield to at most 4 generations for your plugin, they cost credits. Look at
`plugins/punch/design/BRIEF.md` and `plugins/punch/design/editor-preview.png` for the house look: near-black plate
`#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`, Barlow Condensed labels, a character or object in the centre
that fits the effect (Punch has a boxer because the OP-1 screen for Punch is a boxer). Pick the visual idea from the OP-1
effect itself (its screen, its name). No real person's likeness, no logos, no text baked into the art. Never show the Claude
Design `serve_url` to the user, only the `https://claude.ai/design/p/<project>` link.

## House rules

- Never write an em dash or en dash anywhere (code, comments, docs, commit text). Use a hyphen.
- Docs and comments in first person singular if they need a voice at all, plain and short. No marketing words.
- Vendor "Bombest", plugin manufacturer code `Bomb`, bundle id `audio.bombest.<id>`, CLAP id `audio.bombest.<id>`.
- Four-character plugin code you pick must be unique across effects: grid `Grid`, delay `Dely`, phone `Phon`, spring `Sprg`,
  cwo `Cwo1`, nitro `Ntro`, punch `Pnch`.

## Report back

Reply with: what you built, the files, test and validator results with the real output lines, the design project link,
and every thing you could not verify. Do not claim a format works unless you ran its validator.
