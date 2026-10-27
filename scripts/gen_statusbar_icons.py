#!/usr/bin/env python3
"""gen_statusbar_icons.py — Carbon battery icons -> LVGL A8 masks (generated).

Input : assets/images/icons/battery_low.png
        assets/images/icons/battery_half.png
        assets/images/icons/battery_full.png
        (Carbon Design System `battery-low` / `battery-half` / `battery-full`,
         rasterised once at 64x64 from the Iconify SVG set; Apache-2.0.)

         Reproduce the rasters with (rsvg-convert, 2.62+):
           rsvg-convert -w 64 -h 64 -o battery_full.png carbon-battery-full.svg
         after replacing each SVG's fill="currentColor" with fill="#ffffff".
         The PNGs are committed, so the build and this generator never need a
         rasteriser on the host.
Output: src/ui/generated/StatusBarIcons.generated.h

Why masks, not bitmaps: these are single-colour silhouettes, so all the shape lives
in the alpha channel. An A8 mask cropped to its ink bbox is a few hundred bytes and
is recoloured at draw time (`lv_obj_set_style_image_recolor`), so the ink colour has
exactly one source — the theme — and the same asset serves a dark bar, a Casio bar
or a low-battery warning tint without a second file.

Usage:
    python3 scripts/gen_statusbar_icons.py           # write the header
    python3 scripts/gen_statusbar_icons.py --check   # drift gate

Exit codes: 0 ok · 1 bad input asset · 2 stale (--check).
Stdlib only (zlib), like scripts/gen_casio_arrow_masks.py.
"""

import argparse
import os
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "assets", "images", "icons")
OUT = os.path.join(REPO, "src", "ui", "generated", "StatusBarIcons.generated.h")

ALPHA_CUTOFF = 96     # anything >= this is ink
TARGET_H = 9          # pixels tall inside an 18px status bar

STATES = [
    ("low",  "battery_low.png"),
    ("half", "battery_half.png"),
    ("full", "battery_full.png"),
]


from pngmask import ALPHA_CUTOFF, decode_png_rgba, ink_bbox, resample  # noqa: E402


def build_header():
    masks = []
    for key, fname in STATES:
        path = os.path.join(SRC, fname)
        if not os.path.exists(path):
            sys.exit("missing input asset: %s" % path)
        w, h, rgba = decode_png_rgba(path)
        bbox = ink_bbox(w, h, rgba)
        bw, bh = bbox[2] - bbox[0], bbox[3] - bbox[1]
        tw = max(1, round(TARGET_H * bw / bh))
        masks.append((key, tw, TARGET_H, resample(w, h, rgba, bbox, tw, TARGET_H)))

    widths = {m[1] for m in masks}
    height = masks[0][2]
    assert len(widths) == 1, "the three states must rasterise to one width: %s" % widths
    w = widths.pop()

    L = []
    L.append("/*")
    L.append(" * StatusBarIcons.generated.h — GENERATED FILE, DO NOT EDIT.")
    L.append(" *")
    L.append(" * Source : assets/images/icons/battery_{low,half,full}.png")
    L.append(" *          Carbon Design System battery icons (Apache-2.0), via Iconify.")
    L.append(" * Script : scripts/gen_statusbar_icons.py")
    L.append(" * Regenerate: python3 scripts/gen_statusbar_icons.py")
    L.append(" * Drift gate: python3 scripts/gen_statusbar_icons.py --check")
    L.append(" *")
    L.append(" * A8 ink masks, %dx%d, recoloured at draw time from the theme." % (w, height))
    L.append(" */")
    L.append("")
    L.append("#pragma once")
    L.append("")
    L.append("#include <lvgl.h>")
    L.append("")
    L.append("namespace ui {")
    L.append("")
    L.append("inline constexpr int kBatteryIconW = %d;" % w)
    L.append("inline constexpr int kBatteryIconH = %d;" % height)
    L.append("")
    for key, mw, mh, data in masks:
        name = key.capitalize()
        L.append("static const uint8_t k%sMap[] = {" % name)
        for i in range(0, len(data), w):
            row = data[i:i + w]
            L.append("    " + ", ".join("0x%02X" % b for b in row) + ",")
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
    with open(OUT, "w") as fh:
        fh.write(header)
    print("wrote %s (%d bytes)" % (OUT, len(header)))


if __name__ == "__main__":
    main()
