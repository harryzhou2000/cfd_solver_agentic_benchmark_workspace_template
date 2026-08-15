"""Minimal pure-stdlib PNG writer (RGBA, 8-bit)."""

import struct
import zlib


def write_png(path, width, height, pixel_rows):
    """pixel_rows: iterable of rows; each row is an iterable of (r,g,b,a)."""
    raw = bytearray()
    for row in pixel_rows:
        raw.append(0)  # filter: None
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(typ, data):
        out = struct.pack(">I", len(data)) + typ + data
        out += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
        return out

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
