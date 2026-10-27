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


