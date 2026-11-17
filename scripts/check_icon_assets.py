#!/usr/bin/env python3
"""check_icon_assets.py — validate the PNGs the mask generators consume.

The generators do no scaling: a 32px file ships 32px wide. That means a wrong-sized
or soft-edged asset does not fail anywhere — it silently produces a blurry chip. This
script is the gate: it checks every icon against the size the UI actually reserves,
that alpha is strictly 0 or 255 (no soft edges to become grey mush at 4bpp), that
there is a 1px transparent margin (masks crop to the ink bbox, so ink on the edge
gets clipped), and that the glyph has some ink at all.

Usage:
    python3 scripts/check_icon_assets.py            # all assets
    python3 scripts/check_icon_assets.py --strict   # also fail on warnings

Exit: 0 ok · 1 a requirement failed · 2 a file is missing.
Stdlib only, like the other generators.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngmask import decode_png_rgba  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(REPO, "assets", "images", "icons")

# (file, expected w, expected h, required?)
SPEC = [
    ("tab_table.png",               16, 14, True),
    ("tab_graph.png",               16, 14, True),
    ("tab_symbols.png",             16, 14, False),
    ("mode_ddx.png",                26, 40, True),
    ("mode_integral.png",           24, 24, True),
    ("battery_low.png",             16, 10, True),
    ("battery_half.png",            16, 10, True),
    ("battery_full.png",            16, 10, True),
    ("help_question.png",            8, 12, False),
]

# The names the generators currently read (kept so the check is useful today).
LEGACY = {
    "tab_table.png":              "tab_table.png",
    "tab_graph.png":              "tab_chart_line.png",
    "tab_symbols.png":            "tab_square_root_variable.png",
    "mode_ddx.png":               "calculus_ddx.png",
    "mode_integral.png":          "calculus_integral.png",
}


def check(path, want_w, want_h):
    problems, warnings = [], []
    if not os.path.exists(path):
        return ["missing"], warnings
    w, h, rgba = decode_png_rgba(path)
    if (w, h) != (want_w, want_h):
        problems.append("size %dx%d, spec says %dx%d" % (w, h, want_w, want_h))
    xs, ys, soft = [], [], 0
    for y in range(h):
        for x in range(w):
            a = rgba[(y * w + x) * 4 + 3]
            if 0 < a < 255:
                soft += 1
            if a == 255:
                xs.append(x); ys.append(y)
    if soft:
        problems.append("%d anti-aliased pixels (alpha must be 0 or 255)" % soft)
    if not xs:
        problems.append("no opaque ink")
    else:
        if min(xs) == 0 or min(ys) == 0 or max(xs) == w - 1 or max(ys) == h - 1:
            problems.append("ink touches the border (needs a 1px transparent margin)")
        if (max(xs) - min(xs) + 1) > want_w * 0.95:
            warnings.append("ink fills the full width; chip will look edge-to-edge")
    return problems, warnings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    failed = missing = 0
    for name, w, h, required in SPEC:
        on_disk = os.path.join(DIR, LEGACY.get(name, name))
        problems, warnings = check(on_disk, w, h)

        if problems == ["missing"]:
            if required:
                print("MISSING  %-26s expected at %s" % (name, on_disk))
                missing += 1
            else:
                print("skip     %-26s (optional, not supplied)" % name)
            continue

        if problems:
            print("FAIL     %-26s %s" % (name, "; ".join(problems)))
            failed += 1
            continue

        note = ("  [" + "; ".join(warnings) + "]") if warnings else ""
        print("ok       %-26s %dx%d%s" % (name, w, h, note))
        if warnings and args.strict:
            failed += 1

    print("\n%d ok, %d failed, %d missing" % (len(SPEC) - failed - missing, failed, missing))
    if failed:
        return 1
    if missing:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
