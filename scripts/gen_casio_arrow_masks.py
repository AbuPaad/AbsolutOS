#!/usr/bin/env python3
"""gen_casio_arrow_masks.py — PNG page-turn arrow -> LVGL A8 mask (generated).

Input : assets/images/casio_page_arrow_right.png   (operator-supplied asset)
Output: src/ui/generated/CasioArrowMasks.generated.h

Why a mask, not a bitmap: the supplied asset is a single-colour silhouette — every
inked pixel is the same black and all the shape lives in the alpha channel. Storing
RGB would be pure waste (96x96 RGBA = 36,864 B). As an A8 mask, cropped to its ink
bounding box, the same mark costs 960 B and is recoloured at draw time from the
theme's text token (`image_recolor`), so the ink colour has exactly one source.

The left arrow is the horizontal mirror of the right one, produced here at
generation time (no runtime scaling or rotation on device).

Usage:
    python3 scripts/gen_casio_arrow_masks.py            # write the header
    python3 scripts/gen_casio_arrow_masks.py --check    # drift gate (CI/agent)

Exit codes: 0 ok · 1 bad input asset · 2 stale (--check).
Stdlib only (zlib), like scripts/compare-ppm.py.
"""

import argparse
import os
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "assets", "images", "casio_page_arrow_right.png")
OUT = os.path.join(REPO, "src", "ui", "generated", "CasioArrowMasks.generated.h")

ALPHA_CUTOFF = 128   # 1-bit decision: anything >= this is ink
STRIP_ARROW_H = 12   # strip history arrow: the mark, scaled down (px tall)


def decode_png_rgba(path):
    """Minimal PNG reader: 8-bit RGBA (colortype 6), non-interlaced. Returns (w,h,bytes)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            ihdr = body
        elif ctype == b"IDAT":
            idat += body
        pos += 12 + length
    if ihdr is None:
        raise ValueError("no IHDR")
    w, h, depth, ctype, comp, filt, interlace = struct.unpack(">IIBBBBB", ihdr)
    if depth != 8 or ctype != 6 or interlace != 0:
        raise ValueError("need 8-bit RGBA, non-interlaced (got depth=%d colortype=%d interlace=%d)"
                         % (depth, ctype, interlace))
    raw = zlib.decompress(idat)
    stride, bpp = w * 4, 4
    out, prev, p = bytearray(), bytearray(stride), 0
    for _ in range(h):
        ft = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ft == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif ft == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out += line
        prev = line
    return w, h, bytes(out)


def analyze(w, h, rgba):
    """Return (bbox, alpha_levels, distinct_inks)."""
    xs, ys = [], []
    levels, inks = set(), set()
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            a = rgba[o + 3]
            levels.add(a)
            if a > 0:
                xs.append(x); ys.append(y)
                inks.add(rgba[o:o + 3])
    if not xs:
        raise ValueError("asset is fully transparent — nothing to convert")
    return (min(xs), max(xs), min(ys), max(ys)), sorted(levels), inks


def mask_bytes(w, h, rgba, x0, y0, cw, ch, mirror):
    """A8 mask, stride == width, cropped to the ink bbox (optionally mirrored)."""
    out = bytearray()
    for row in range(ch):
        for col in range(cw):
            sx = (x0 + (cw - 1 - col)) if mirror else (x0 + col)
            sy = y0 + row
            o = (sy * w + sx) * 4
            out.append(255 if rgba[o + 3] >= ALPHA_CUTOFF else 0)
    return bytes(out)


def cw(mask, w, h):
    """Rotate an A8 mask 90 degrees clockwise -> (mask, new_w, new_h)."""
    out = bytearray(h * w)
    for y2 in range(w):
        for x2 in range(h):
            out[y2 * h + x2] = mask[(h - 1 - x2) * w + y2]
    return bytes(out), h, w


def rotate90(mask, w, h, quarters):
    """Quarter turns: +1 clockwise, -1 counter-clockwise, +/-2 half."""
    q = quarters % 4
    if q > 2:
        q -= 4
    for _ in range(abs(q)):
        mask, w, h = (cw(mask, w, h) if q > 0
                      else cw(*cw(*cw(mask, w, h))))
    return mask, w, h


def scale_nearest(mask, w, h, target_h):
    """Aspect-preserving nearest-neighbour rescale to a target height.

    Nearest neighbour on purpose: the mark is a 1-bit silhouette, so any
    interpolation would invent grey edge pixels the mask format cannot hold.
    """
    nh = max(1, int(target_h))
    nw = max(1, int(round(w * (nh / float(h)))))
    out = bytearray(nw * nh)
    for y in range(nh):
        sy = min(h - 1, int(y * h / float(nh)))
        for x in range(nw):
            sx = min(w - 1, int(x * w / float(nw)))
            out[y * nw + x] = mask[sy * w + sx]
    return bytes(out), nw, nh


def emit_array(name, blob, per_line=16):
    lines = []
    for i in range(0, len(blob), per_line):
        chunk = blob[i:i + per_line]
        lines.append("    " + " ".join("0x%02X," % b for b in chunk))
    body = "\n".join(lines)
    return ("static const uint8_t %s[] = {\n%s\n};\n" % (name, body))


def render():
    w, h, rgba = decode_png_rgba(SRC)
    (x0, x1, y0, y1), levels, inks = analyze(w, h, rgba)
    cw, ch = x1 - x0 + 1, y1 - y0 + 1
    right = mask_bytes(w, h, rgba, x0, y0, cw, ch, False)
    left = mask_bytes(w, h, rgba, x0, y0, cw, ch, True)

    # Strip variants (history hint): the SAME asset, one quarter turn, scaled down.
    # up  = -90 deg (older entries exist), down = +90 deg (paged back into history).
    up = scale_nearest(*rotate90(right, cw, ch, -1), target_h=STRIP_ARROW_H)
    down = scale_nearest(*rotate90(right, cw, ch, +1), target_h=STRIP_ARROW_H)

    o = []
    o.append("/*")
    o.append(" * CasioArrowMasks.generated.h — GENERATED FILE, DO NOT EDIT BY HAND.")
    o.append(" *")
    o.append(" * Source asset: assets/images/casio_page_arrow_right.png")
    o.append(" *   %dx%d RGBA, ink bbox x[%d..%d] y[%d..%d] -> cropped to %dx%d"
             % (w, h, x0, x1, y0, y1, cw, ch))
    o.append(" *   alpha levels present: %s  ·  distinct ink colours: %d"
             % (levels, len(inks)))
    o.append(" * Regenerate:   python3 scripts/gen_casio_arrow_masks.py")
    o.append(" *")
    o.append(" * A8 alpha masks (stride == width). The ink colour comes from the theme at")
    o.append(" * draw time via lv_obj_set_style_image_recolor(), so the same mask serves any")
    o.append(" * ink token. `left` is the horizontal mirror of `right`, made here rather than")
    o.append(" * scaled/rotated on device.")
    o.append(" */")
    o.append("")
    o.append("#pragma once")
    o.append("")
    o.append("#include <lvgl.h>")
    o.append("")
    o.append("namespace ui {")
    o.append("")
    o.append("inline constexpr int kCasioArrowW = %d;" % cw)
    o.append("inline constexpr int kCasioArrowH = %d;" % ch)
    o.append("")
    o.append(emit_array("kCasioArrowRightMap", right))
    o.append(emit_array("kCasioArrowLeftMap", left))
    o.append(emit_array("kCasioArrowUpMap", up[0]))
    o.append(emit_array("kCasioArrowDownMap", down[0]))
    o.append("inline const lv_image_dsc_t kCasioArrowRight = {")
    o.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,")
    o.append("                .flags = 0, .w = kCasioArrowW, .h = kCasioArrowH,")
    o.append("                .stride = kCasioArrowW },")
    o.append("    .data_size = sizeof(kCasioArrowRightMap),")
    o.append("    .data = kCasioArrowRightMap,")
    o.append("};")
    o.append("inline const lv_image_dsc_t kCasioArrowLeft = {")
    o.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,")
    o.append("                .flags = 0, .w = kCasioArrowW, .h = kCasioArrowH,")
    o.append("                .stride = kCasioArrowW },")
    o.append("    .data_size = sizeof(kCasioArrowLeftMap),")
    o.append("    .data = kCasioArrowLeftMap,")
    o.append("};")
    o.append("inline constexpr int kCasioArrowUpW = %d;" % up[1])
    o.append("inline constexpr int kCasioArrowUpH = %d;" % up[2])
    o.append("inline const lv_image_dsc_t kCasioArrowUp = {")
    o.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,")
    o.append("                .flags = 0, .w = kCasioArrowUpW, .h = kCasioArrowUpH,")
    o.append("                .stride = kCasioArrowUpW },")
    o.append("    .data_size = sizeof(kCasioArrowUpMap),")
    o.append("    .data = kCasioArrowUpMap,")
    o.append("};")
    o.append("inline const lv_image_dsc_t kCasioArrowDown = {")
    o.append("    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,")
    o.append("                .flags = 0, .w = kCasioArrowUpW, .h = kCasioArrowUpH,")
    o.append("                .stride = kCasioArrowUpW },")
    o.append("    .data_size = sizeof(kCasioArrowDownMap),")
    o.append("    .data = kCasioArrowDownMap,")
    o.append("};")
    o.append("")
    o.append("} // namespace ui")
    o.append("")
    return "\n".join(o), (cw, ch, len(inks), len(levels))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if not os.path.exists(SRC):
        print("asset not found: %s" % SRC, file=sys.stderr)
        return 1
    try:
        text, info = render()
    except ValueError as exc:
        print("gen_casio_arrow_masks: %s" % exc, file=sys.stderr)
        return 1
    cw, ch, inks, levels = info
    if inks > 1:
        print("gen_casio_arrow_masks: warning — %d distinct ink colours; the mask keeps "
              "only coverage, so the RGB is discarded" % inks, file=sys.stderr)
    if args.check:
        cur = ""
        if os.path.exists(OUT):
            with open(OUT, "r", encoding="utf-8") as fh:
                cur = fh.read()
        if cur != text:
            print("gen_casio_arrow_masks: %s is stale — rerun without --check" % OUT,
                  file=sys.stderr)
            return 2
        print("gen_casio_arrow_masks: up to date (%dx%d mask)" % (cw, ch))
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_casio_arrow_masks: wrote %s (%dx%d A8 mask, %d B each)"
          % (os.path.relpath(OUT, REPO), cw, ch, cw * ch))
    return 0


if __name__ == "__main__":
    sys.exit(main())
