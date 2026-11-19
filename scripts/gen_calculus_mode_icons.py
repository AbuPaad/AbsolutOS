#!/usr/bin/env python3
"""gen_calculus_mode_icons.py — calculus mode glyphs -> LVGL A8 masks (generated).

Input : assets/images/icons/calculus_ddx.png        operator-supplied d/dx mark
        assets/images/icons/calculus_integral.png   tabler `math-integral-x` (MIT)
Output: src/ui/generated/CalculusModeIcons.generated.h

Both are single-colour silhouettes, so they ship as A8 masks cropped to their ink
bounding box and are recoloured at draw time — one asset serves the selected cell
(white ink on accent) and the unselected cell (dim ink on pane) with no second file.

Reproduce the integral raster (rsvg-convert, librsvg 2.62+):
    sed 's/currentColor/#ffffff/g' math-integral-x.svg > /tmp/i.svg
    rsvg-convert -h 96 -w 96 -o calculus_integral.png /tmp/i.svg

Usage:
    python3 scripts/gen_calculus_mode_icons.py           # write the header
    python3 scripts/gen_calculus_mode_icons.py --check   # drift gate

Exit codes: 0 ok · 1 bad input asset · 2 stale (--check).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngmask import decode_png_rgba, ink_bbox, integer_scale, resample  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "assets", "images", "icons")
OUT = os.path.join(REPO, "src", "ui", "generated", "CalculusModeIcons.generated.h")

# (key, file, target pixel height inside the 20px-tall mode chip)
ICONS = [
    ("Deriv",    "calculus_ddx.png",      40),   # 1:1 with the master — no resampling
    ("Integral", "calculus_integral.png", 24),   # 1:1 with the 24px tabler master
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
        tw, th = integer_scale(bw, bh, th)   # integer division of the master
        built.append((key, tw, th, resample(w, h, rgba, bbox, tw, th)))

    L = [
        "/*",
        " * CalculusModeIcons.generated.h — GENERATED FILE, DO NOT EDIT.",
        " *",
        " * Sources: assets/images/icons/calculus_ddx.png      (operator-supplied mark)",
        " *          assets/images/icons/calculus_integral.png (tabler math-integral-x, MIT)",
        " * Script : scripts/gen_calculus_mode_icons.py",
        " * Regenerate: python3 scripts/gen_calculus_mode_icons.py",
        " * Drift gate: python3 scripts/gen_calculus_mode_icons.py --check",
        " *",
        " * A8 ink masks, recoloured at draw time from the theme, so the selected and",
        " * unselected chip cells share one asset.",
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
        L.append("inline constexpr int kCalc%sW = %d;" % (key, w))
        L.append("inline constexpr int kCalc%sH = %d;" % (key, h))
    L.append("")
    for key, w, h, data in built:
        L.append("static const uint8_t kCalc%sMap[] = {" % key)
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
        if not os.path.exists(OUT):
            sys.exit(2)
        if open(OUT).read() != header:
            sys.stderr.write("STALE: %s differs from its sources\n" % OUT)
            sys.exit(2)
        print("ok: %s is up to date" % OUT)
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write(header)
    print("wrote %s (%d bytes)" % (OUT, len(header)))


if __name__ == "__main__":
    main()
