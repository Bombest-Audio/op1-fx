# Handoff: op1-fx

Repo: `~/bombest-audio/op1-fx` (private `Bombest-Audio/op1-fx`, branch main, last commit 3ed7486). Read `AGENTS.md` and `~/.claude/CLAUDE.md` style rules first: short and direct, no em or en dashes anywhere (hyphens only), first person singular in docs, commit messages in conventional format, never push without asking, never touch money or signing identities.

## What exists

20 plugins, each in `plugins/<id>/`: grid, punch, delay, phone, spring, cwo, nitro (effects) and fm, cluster, pulse, phase, string, voltage, dsynth, digital, dna, drwave, dbox, sampler, drumsampler (instruments). They are new implementations inspired by the OP-1 and OP-1 Field, not bit-exact. Each folder has a portable C99 `core/`, a JUCE plugin in `plugin/`, an MPC VST2 port in `mpc/`, art in `design/`, and docs. The shared recipe is `cmake/op1fx.cmake`. Briefs for adding a plugin: `docs/PORTING-BRIEF.md`, `docs/PORTING-BRIEF-SYNTH.md`. MPC animation brief: `docs/MPC-ANIMATION-BRIEF.md`. AAX notes: `docs/AAX.md`.

## State (2026-10-30)

- macOS: VST3, AU, CLAP, Standalone build for all 20. All 20 AUs pass `auval`. pluginval 8 and clap-validator pass on all.
- AAX: builds unsigned with `-DOP1FX_AAX_SDK=$HOME/bombest-audio/aax-sdk-2-9-0`. Signing needs a PACE Central WCGUID per plugin from the user. wraptool 6 needs `--password`, see `~/.claude/rules/aax-signing.md`.
- Windows x64: built on the UTM VM `hmd-win` and all 20 VST3s pass pluginval 8 (`--skip-gui-tests`). Binaries are in `dist/windows-x64/` (git ignores `dist/`). Windows arm64 and Windows AAX are not built.
- MPC: all 20 aarch64 0.2.0 zips are installed on the Live III. Zips are in `dist/mpc/`.
- MPC skins animate: knob-driven filmstrip meters plus a signal-driven `act` meter. `common/op1fx_act.h` (copied into each `mpc/` folder by `tools/add_mpc_activity.py`) wraps each engine and publishes the frame. Every knob-to-element motion is a design choice, never seen on the device.
- cwo has a MIX knob (default 100% wet).

## Not done

1. The user wants animation tweaks. They have not said what to change yet. Ask which plugin and what looks wrong before editing. Nothing has been looked at on the device.
2. Windows arm64 build and pluginval, Windows AAX.
3. AAX signing (blocked on WCGUIDs from the user).
4. GitHub Actions workflow in `.github/workflows/build.yml` has never run. Check whether Actions is enabled on the repo, do not enable it without asking.
5. No listening comparison with a real OP-1, nothing loaded on an MPC track.
6. Known cosmetic: Voltage MPC layout warns that the ATTACK and DECAY touch areas overlap by 12x124 px. clap-validator sometimes warns about a `...x1` plugin id suffix (wrapper quirk).

## How to do the common jobs

macOS build of everything:

```bash
cd ~/bombest-audio/op1-fx
cmake -S . -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE \
  -DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=$PWD/.deps/clap-juce-extensions
cmake --build build/all --parallel 8
```

One plugin: add `-DOP1FX_ONLY=<id>` and use `build/<id>`. Check `vtool -show-build` shows `minos 11.0` and `lipo -info` shows x86_64 arm64.

AU check: copy the `.component` to `~/Library/Audio/Plug-Ins/Components`, `killall -9 AudioComponentRegistrar`, run `auval -v aufx|aumu <Code> Bomb` (effects aufx, instruments aumu, codes in each `plugins/<id>/CMakeLists.txt`), then remove the copy.

MPC port (toolchain `~/bombest-audio/mpc-re/mpc-vst-plugins`, needs Docker, one Docker build at a time):

```bash
cd ~/bombest-audio/mpc-re/mpc-vst-plugins
tools/test_port.sh ~/bombest-audio/op1-fx/plugins/<id>/mpc/vst.json      # must print PASSED
tools/build_port.sh ~/bombest-audio/op1-fx/plugins/<id>/mpc/vst.json all
python3 tools/release.py --so <mpc>/build/aarch64/<id>.so --skin "<mpc>/build/skin/bombest - VST - <Name>" \
  --entry <mpc>/build/pluginlist-entry.xml --version 0.2.0 --about "..." [--extra <mpc>/samples:samples] -o ~/bombest-audio/op1-fx/dist/mpc
```

Do not edit `params.json` order (append only, see `params.lock`). Skin layout y = skin y + 86. No skin image taller than 16384 px (meter strips 12288).

Install on a Live III (root SSH, MPC OS 5.0.17; use your device's address and SSH port): scp the zips to `/nvme/stage`, stop the MPC service (`systemctl stop acvs`), run `sh install.sh -y -n -t /nvme/Synths` inside each unzipped folder, then `systemctl start acvs`. The MPC can restart by itself mid-batch, which makes `-n` installs fail with "MPC is running"; stop it again and retry the failed ones. BusyBox here: use `head -n N`. Installing restarts the MPC and loses unsaved projects, so ask the user first.

Windows (VM `hmd-win`, ssh alias, start with `/Applications/UTM.app/Contents/MacOS/utmctl start hmd-win`): source tarball goes to `C:\Users\thomas\op1fx` (strip `._*` AppleDouble files after untar, they break the globs). `winbuild.bat x64|arm64` configures and builds under vcvarsall into `build\win-x64`, log in `build-x64.log`. Run long builds from a foreground ssh in a background local shell, `start /b` dies with the session. pluginval is at `C:\Users\thomas\punch-build\pv\pluginval.exe`. MinGW does not work, JUCE 8 needs MSVC.

## Rules that bit me

- zsh does not word-split unquoted variables, use arrays or direct commands.
- Comments must match the code, interpolations continuous, no clicks, headroom rechecked (`~/.claude/rules/code-review-lessons.md`).
- Windows pluginval was run with `--skip-gui-tests`, say so when you report it.
- Report only what you ran. Say what is unverified.
