#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure non-background ink in a binary P6 PPM.

Usage: python3 scripts/measure-ink.py <file.ppm> --region x0,y0,x1,y1
       [--clip x0,y0,x1,y1] [--json]

Region/clip coordinates are half-open: [x0, x1) x [y0, y1). Background is the
most common colour inside --region. Prints the ink bounding box and pixel
count; with --clip, exits 1 if any ink falls outside the clip rect.
"""

import argparse
import struct
import sys
from collections import Counter


def read_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError("not a P6 PPM")
        # skip comments
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        maxval = int(f.readline())
        data = f.read()
    if maxval != 255:
        raise ValueError("only 8-bit PPM supported")
    return w, h, data


def parse_rect(s):
    x0, y0, x1, y1 = (int(v) for v in s.split(","))
    return x0, y0, x1, y1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--region", required=True)
    ap.add_argument("--clip", default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    w, h, data = read_ppm(args.file)
    rx0, ry0, rx1, ry1 = parse_rect(args.region)
    rx0, ry0 = max(0, rx0), max(0, ry0)
    rx1, ry1 = min(w, rx1), min(h, ry1)

    # background = most common colour in region
    counts = Counter()
    for y in range(ry0, ry1):
        for x in range(rx0, rx1):
            i = (y * w + x) * 3
            counts[(data[i], data[i + 1], data[i + 2])] += 1
    bg = counts.most_common(1)[0][0] if counts else (0, 0, 0)

    ix0 = iy0 = ix1 = iy1 = None
    n = 0
    outside = False
    clip = parse_rect(args.clip) if args.clip else None
    for y in range(ry0, ry1):
        for x in range(rx0, rx1):
            i = (y * w + x) * 3
            if (data[i], data[i + 1], data[i + 2]) == bg:
                continue
            n += 1
            if ix0 is None or x < ix0:
                ix0 = x
            if iy0 is None or y < iy0:
                iy0 = y
            if ix1 is None or x > ix1:
                ix1 = x
            if iy1 is None or y > iy1:
                iy1 = y
            if clip is not None:
                cx0, cy0, cx1, cy1 = clip
                if not (cx0 <= x < cx1 and cy0 <= y < cy1):
                    outside = True

    if n == 0:
        ink = None
    else:
        ink = {"x0": ix0, "y0": iy0, "x1": ix1, "y1": iy1}

    if args.json:
        import json
        print(json.dumps({"ink": ink, "count": n}))
    else:
        print(f"ink={ink} count={n}")

    if clip is not None and outside:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()