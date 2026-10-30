#!/usr/bin/env python3
"""Add the live activity meter to every plugin's MPC port (safe to run again).

For each plugins/<id>/mpc: wraps the engine table with common/op1fx_act.h, adds the engine-driven "act" parameter,
turns on HAS_DISPLAY_REV in vst.json, draws the 64-frame filmstrip in the plugin's accent colour and puts a `meter`
line under the art on the first tab. Frame = lead knob step * 8 + signal level step: the ring sweeps as the lead
Q-Link turns, the glow rises with the output level.
"""
import json
import re
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
W, H = 500, 180
KS, LS = 8, 8


def strip(accent, out):
    rgb = tuple(int(accent[i:i + 2], 16) for i in (0, 2, 4))
    sheet = Image.new("RGBA", (W, H * KS * LS), (0, 0, 0, 0))
    bbox = (30, 20, W - 30, H - 20)
    for k in range(KS):
        for l in range(LS):
            glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            a = int(150 * (l / (LS - 1)) ** 1.2)
            if a:
                ImageDraw.Draw(glow).ellipse((60, 36, W - 60, H - 36), fill=rgb + (a,))
                glow = glow.filter(ImageFilter.GaussianBlur(26))
            ring = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            rd = ImageDraw.Draw(ring)
            rd.ellipse(bbox, outline=rgb + (28,), width=2)
            sweep = (k + 1) / KS * 360.0
            rd.arc(bbox, -90, -90 + sweep, fill=rgb + (150,), width=3)
            glow.alpha_composite(ring)
            sheet.paste(glow, (0, (k * LS + l) * H))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def patch(pdir):
    mpc = pdir / "mpc"
    params = json.loads((mpc / "params.json").read_text())
    keys = [p["key"] for p in params["params"]]
    if "act" not in keys:
        params["params"].append({"key": "act", "name": "Activity", "min": 0, "max": 1, "default": 0})
        (mpc / "params.json").write_text(json.dumps(params, indent=2) + "\n")
    lead = keys[0]

    vst = json.loads((mpc / "vst.json").read_text())
    d = vst.setdefault("defines", {})
    d["HAS_DISPLAY_REV"] = 1
    d["DISPLAY_REV_NO_UPDATE"] = 1
    fl = vst["build"]["cflags"]
    if "-I../../common" in fl:
        fl.remove("-I../../common")
    # the port is built in a container that only sees the plugin folder, so the header is copied next to engine.c
    shutil.copyfile(ROOT / "common" / "op1fx_act.h", mpc / "op1fx_act.h")
    (mpc / "vst.json").write_text(json.dumps(vst, indent=2) + "\n")

    eng = (mpc / "engine.c").read_text()
    if "op1fx_act.h" not in eng:
        eng = eng.replace('#include "engine.h"\n', '#include "engine.h"\n#include "op1fx_act.h"\n', 1)
        eng, n1 = re.subn(r"static const mpc_engine_t ENGINE = \{", "static const mpc_engine_t ENGINE_BASE = {", eng)
        eng, n2 = re.subn(r"return &ENGINE;", f'return op1_act_wrap(&ENGINE_BASE, "{lead}");', eng)
        if n1 != 1 or n2 != 1 or "op1fx_act.h" not in eng:
            sys.exit(f"{pdir.name}: engine.c patch anchors not found ({n1}, {n2})")
        (mpc / "engine.c").write_text(eng)

    lay = (mpc / "layout.conf").read_text()
    accent = re.search(r"theme_accent=([0-9a-fA-F]{6})", lay).group(1)
    strip(accent, mpc / "images" / "act.png")
    if "key=act" not in lay:
        line = f"meter cx=640 cy=330 w={W} h={H} key=act strip=images/act.png frames={KS * LS}"
        lay = re.sub(r"(\[tab [^\n]*\]\nart [^\n]*\n)", lambda m: m.group(1) + line + "\n", lay, count=1)
        if "key=act" not in lay:
            sys.exit(f"{pdir.name}: no art line after the first tab")
        (mpc / "layout.conf").write_text(lay)
    print(f"{pdir.name}: lead={lead} accent={accent}")


if __name__ == "__main__":
    only = sys.argv[1:]
    for pdir in sorted((ROOT / "plugins").iterdir()):
        if (pdir / "mpc" / "engine.c").exists() and (not only or pdir.name in only):
            patch(pdir)
