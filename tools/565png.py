#!/usr/bin/env python3
"""Turn raw RGB565 frames (410 x 502, little-endian) into PNGs: 565png.py <dir> ..."""
import os
import struct
import sys

from PIL import Image

W, H = 410, 502
for d in sys.argv[1:]:
    for n in sorted(os.listdir(d)):
        if not n.endswith(".565"):
            continue
        raw = open(os.path.join(d, n), "rb").read()
        px = struct.unpack("<%dH" % (W * H), raw[: W * H * 2])
        img = Image.new("RGB", (W, H))
        img.putdata([(((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3) for v in px])
        img.save(os.path.join(d, n[:-4] + ".png"))
        os.remove(os.path.join(d, n))
