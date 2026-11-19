#!/usr/bin/env python3
"""pngmask.py — shared PNG -> LVGL A8 mask helpers (stdlib only, no Pillow).

Used by scripts/gen_statusbar_icons.py and scripts/gen_calculus_mode_icons.py.
Kept in one place so the decode/resample rules cannot drift between generators.
"""

import struct
import zlib

ALPHA_CUTOFF = 96
def decode_png_rgba(path):
    """Minimal PNG reader: 8-bit RGBA (colortype 6), non-interlaced. -> (w, h, bytes)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: %s" % path)
    pos, w, h, colortype, idat = 8, 0, 0, None, bytearray()
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            w, h, bitdepth, colortype = struct.unpack(">IIBB", chunk[:10])
            if bitdepth != 8 or colortype != 6:
                raise ValueError("need 8-bit RGBA (colortype 6), got depth %d type %d"
                                 % (bitdepth, colortype))
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(bytes(idat))
    stride = w * 4
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


def ink_bbox(w, h, rgba):
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if rgba[(y * w + x) * 4 + 3] >= ALPHA_CUTOFF:
                xs.append(x); ys.append(y)
    if not xs:
        raise ValueError("no ink found")
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def resample(w, h, rgba, bbox, tw, th):
    """Nearest-neighbour box average: keeps a 1-bit silhouette crisp (no invented greys)."""
    x0, y0, x1, y1 = bbox
    bw, bh = x1 - x0, y1 - y0
    out = bytearray(tw * th)
    for ty in range(th):
        for tx in range(tw):
            sx0 = x0 + (tx * bw) // tw
            sx1 = x0 + ((tx + 1) * bw) // tw
            sy0 = y0 + (ty * bh) // th
            sy1 = y0 + ((ty + 1) * bh) // th
            acc = n = 0
            for sy in range(sy0, max(sy1, sy0 + 1)):
                for sx in range(sx0, max(sx1, sx0 + 1)):
                    if sx < w and sy < h:
                        acc += rgba[(sy * w + sx) * 4 + 3]; n += 1
            out[ty * tw + tx] = 255 if n and acc / n >= ALPHA_CUTOFF else 0
    return bytes(out)




def integer_scale(bw, bh, target_max):
    """Largest integer downscale whose longer side stays <= target_max.

    Sampling a 64px master into 13px invents staircases: some destination
    columns average two source columns, others three. Constraining the target to
    an integer division means every output pixel is the same clean box of source
    pixels, so strokes stay one pixel and diagonals stay regular. Returns
    (tw, th), never smaller than 1x1.
    """
    if bw <= 0 or bh <= 0:
        return 1, 1
    k = max(1, int(round(max(bw, bh) / float(target_max))))
    return max(1, bw // k), max(1, bh // k)


def area_threshold(bw, bh, alpha, tw, th, coverage=0.40):
    """Area-average downscale with a coverage threshold, tuned for 1-bit strokes.

    Why not point sampling: one source pixel per destination pixel drops or doubles
    strokes depending on alignment, which is what staircases diagonals.

    Why not the plain 50% rule: a 1px diagonal covers ~35-45% of each destination
    cell. At 50% it disappears or breaks into dots. 0.40 keeps hairlines continuous
    while still dropping dust.

    Averaging happens in *linear* light — a gamma-encoded average over-weights the
    bright source pixels, which fattens strokes on dark backgrounds and thins them on
    light ones. For an alpha mask that bias shows up as uneven stroke weight.
    """
    if bw <= 0 or bh <= 0 or tw <= 0 or th <= 0:
        raise ValueError("bad dims")
    out = bytearray(tw * th)
    for ty in range(th):
        sy0 = (ty * bh) // th
        sy1 = max(((ty + 1) * bh) // th, sy0 + 1)
        for tx in range(tw):
            sx0 = (tx * bw) // tw
            sx1 = max(((tx + 1) * bw) // tw, sx0 + 1)
            acc = 0.0; n = 0
            for sy in range(sy0, min(sy1, bh)):
                row = sy * bw
                for sx in range(sx0, min(sx1, bw)):
                    a = alpha[row + sx] / 255.0
                    acc += a * a if False else a   # alpha is already linear coverage
                    n += 1
            out[ty * tw + tx] = 255 if (n and (acc / n) >= coverage) else 0
    return bytes(out)


def erode(mask, w, h):
    """4-neighbour erosion: drops any ink pixel with a transparent orthogonal neighbour.

    Used only when a glyph comes back fatter than 1px after thresholding — it
    removes the salt-and-pepper along an edge but never grows the shape.
    """
    out = bytearray(mask)
    for y in range(h):
        for x in range(w):
            if not mask[y * w + x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= w or ny >= h or not mask[ny * w + nx]:
                    out[y * w + x] = 0
                    break
    return bytes(out)
