#!/usr/bin/env python3
"""Validate a PNG written by tools/common/png.c and print pixel colours.

usage: png_probe.py <file.png> [x y]...
Prints '<width>x<height> ok', then '<x>,<y>: #RRGGBB' for each coordinate pair.
"""
import struct
import sys
import zlib


def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("not a PNG")
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(kind + body) != crc:
            sys.exit(f"bad CRC in {kind.decode()}")
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 3
    if len(raw) != stride * height:
        sys.exit("bad image data length")
    return width, height, raw, stride


def main():
    if len(sys.argv) < 2 or len(sys.argv) % 2:
        sys.exit(__doc__)
    width, height, raw, stride = load(sys.argv[1])
    print(f"{width}x{height} ok")
    coords = sys.argv[2:]
    for i in range(0, len(coords), 2):
        x, y = int(coords[i]), int(coords[i + 1])
        p = y * stride + 1 + x * 3
        print(f"{x},{y}: #{raw[p]:02X}{raw[p + 1]:02X}{raw[p + 2]:02X}")


if __name__ == "__main__":
    main()
