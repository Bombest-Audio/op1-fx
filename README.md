# OP1 plugins

Twenty plugins inspired by the OP-1: seven effects (Cwo, Delay, Grid, Nitro, Phone, Punch, Spring) and thirteen
instruments (Cluster, DBox, DNA, DSynth, Digital, DrWave, DrumSampler, Fm, Phase, Pulse, Sampler, String, Voltage).
Each one has a single portable C core, so the MPC build and the desktop build sound the same.

These are new takes written from scratch. They contain no OP-1 firmware code or tables and are not affiliated with or
endorsed by Teenage Engineering. "OP-1" is their trademark; I use it only to say what inspired these.

## Where they run

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| AAX | macOS, Windows | JUCE, only when you set `OP1FX_AAX_SDK` to your own Avid AAX SDK |
| VST2 | Akai MPC OS standalone devices: Gen2 (aarch64) and Gen1 / Force (armv7) | `plugins/<name>/mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Install on an MPC

Both suites need MPC OS 3.x. The skins use features MPC OS 2.x can't read. Download the suite zip for your MPC from
the latest release:

| MPC | Download |
|---|---|
| Gen2: MPC Live III and later models | `OP1-Suite-<version>-mpc-aarch64.zip` |
| Gen1: Force, MPC Live / Live II, MPC One, MPC X, MPC Key 61 | `OP1-Suite-<version>-mpc-armv7.zip` |

Copy it to the MPC, unzip it, and run this on the device as root:

    sh install-all.sh -y

MPC stops once, all twenty plugins install, MPC starts once. Save your project first. The installer stops if the zip is
for the other generation. `uninstall-all.sh` removes them.

Where they go depends on the MPC OS version:

- **MPC OS 3.10.1:** the installer copies each plugin folder into the internal drive's `VST2 Plugins` folder
  (`/nvme/VST2 Plugins` on Gen2, `/media/acvs-content/VST2 Plugins` on Gen1). Turn on Menu > Preferences > Enhancements >
  Enable Enhancements and External Plugin Support, and the plugins show under External Plugins.
- **MPC OS 3.9.x:** each plugin goes into the Synths folder with an entry in MPC.settings, as before.

`-r vst2` or `-r synths` picks the route yourself. Upgrading from 3.9.x, a plugin already in the Synths folder is updated
too, because projects saved on 3.9.x point at that path. Add `-c` to remove the old copy once your projects open with
the new one. Up to 0.5.0 the plugin folders were named `bombest - VST - <Name>`; the installer treats those as the same
plugins (by uid), so MPC never lists one twice.

Each plugin also installs alone the Synths way from `packages/<name>/install.sh` or its own
`<Name>-<version>-mpc-<arch>.zip`. Its `portable/<folder>/` holds the `plugin.json` 3.10.1 needs, so that folder can also
go into `VST2 Plugins` by hand.

Tested on hardware: Gen2 on an MPC Live III (3.9.1 and the Synths route), and Gen1 on an MPC One (3.10.1, the plugin
folders copied into `VST2 Plugins` by hand). The installer's 3.10.1 route itself hasn't run on a device yet. If something
misbehaves, open an issue.

## Build

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Core tests for a plugin: `plugins/<name>/core/tests/run.sh`. MPC preset save and load for every plugin:
`tools/test_mpc_state.sh`. The MPC builds are covered in `docs/`. The suite zips come from
`tools/make_suite.sh <version>`.

## License

GPL-3.0-only, see `LICENSE`. Third-party notices are in `NOTICE`.

Bombest, Inc.
