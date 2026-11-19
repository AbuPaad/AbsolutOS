#!/usr/bin/env python3
"""gen_grapher_tab_icons.py — Grapher tab glyphs -> LVGL A8 masks (generated).

Input : assets/images/icons/tab_table.png               fa6-solid table
        assets/images/icons/tab_chart_line.png          fa6-solid chart-line
        assets/images/icons/tab_square_root_variable.png fa6-solid square-root-variable
        (Font Awesome Free 6 Solid, CC BY 4.0, rasterised once at 64x64 with
         rsvg-convert; the SVGs are not committed, the PNGs are.)
Output: src/ui/generated/GrapherTabIcons.generated.h

The Grapher's section switcher was three words in a 28px band. It is now three
14px chips floating top-right, so it needs glyphs, not text — and a chip that
holds a mask has no line box, which is what makes it fit in 14px at all.

Reproduce the rasters:
    sed 's/currentColor/#ffffff/g' table.svg > /tmp/t.svg
    rsvg-convert -h 64 -w 64 -o assets/images/icons/tab_table.png /tmp/t.svg

Usage:
    python3 scripts/gen_grapher_tab_icons.py           # write the header
    python3 scripts/gen_grapher_tab_icons.py --check   # drift gate
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngmask import decode_png_rgba, ink_bbox, integer_scale, resample  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "assets", "images", "icons")
OUT = os.path.join(REPO, "src", "ui", "generated", "GrapherTabIcons.generated.h")

ICONS = [
    ("Expr", "tab_table.png", 16),
    ("Graph", "tab_chart_line.png", 16),
    ("Table", "tab_square_root_variable.png", 16),
]


def build_header():
    built = []
    for key, fname, th in ICONS:
        path = os.path.join(SRC, fname)
        if not os.path.exists(path):
            sys.exit("missing input asset: %s" % path)
        w, h, rgba = decode_png_rgba(path)
        bbox = ink_bbox(w, h, rgba)
        bw, bh = bbox[2] - bbox[0], bbox[3] - bbox[1]
        tw, th = integer_scale(bw, bh, th)   # integer division of the 64px master
        built.append((key, tw, th, resample(w, h, rgba, bbox, tw, th)))

    L = [
        "/*",
        " * GrapherTabIcons.generated.h — GENERATED FILE, DO NOT EDIT.",
        " *",
        " * Sources: assets/images/icons/tab_{table,chart_line,square_root_variable}.png",
        " *          Font Awesome Free 6 Solid (CC BY 4.0), rasterised at 64x64.",
        " * Script : scripts/gen_grapher_tab_icons.py",
        " * Regenerate: python3 scripts/gen_grapher_tab_icons.py",
        " * Drift gate: python3 scripts/gen_grapher_tab_icons.py --check",
        " */",
        "",
        "#pragma once",
        "",
        "#include <lvgl.h>",
        "",
        "namespace ui {",
        "",
    ]
    for key, w, h, _ in built:
        L.append("inline constexpr int kTab%sW = %d;" % (key, w))
        L.append("inline constexpr int kTab%sH = %d;" % (key, h))
    L.append("")
    for key, w, h, data in built:
        L.append("static const uint8_t kTab%sMap[] = {" % key)
        for i in range(0, len(data), w):
            L.append("    " + ", ".join("0x%02X" % b for b in data[i:i + w]) + ",")
        L.append("};")
        L.append("")
    L.append("} // namespace ui")
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    header = build_header()
    if a.check:
        if not os.path.exists(OUT) or open(OUT).read() != header:
            sys.stderr.write("STALE: %s differs from its sources\n" % OUT)
            sys.exit(2)
        print("ok: %s is up to date" % OUT)
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write(header)
    print("wrote %s (%d bytes)" % (OUT, len(header)))


if __name__ == "__main__":
    main()
