#!/usr/bin/env python3
"""Compare two PNG pictures pixel by pixel (any PNG colour type, no interlacing).

usage: png_compare.py <a.png> <b.png> [--levels N]
--levels N compares each colour channel rounded to N evenly spaced levels (4 for the Game Boy's greys, 32 for the
Game Boy Color's 5-bit colours), so two pictures of the same thing in slightly different shades still match.
Prints 'same' (exit 0) or the number of differing pixels and the first few (exit 1).
"""
import struct
import sys
import zlib


def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, palette = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if interlace:
                sys.exit(f"{path}: interlaced PNGs are not read")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"PLTE":
            palette = body
        pos += 12 + length
    raw = zlib.decompress(idat)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    step = max(1, channels * depth // 8)
    stride = (width * channels * depth + 7) // 8
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - step] if i >= step else 0
            b = prev[i]
            c = prev[i - step] if i >= step else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line

        def sample(x):  # one sample of 1, 2, 4 or 8 bits
            if depth == 8:
                return line[x]
            return (line[x * depth // 8] >> (8 - depth - (x * depth) % 8)) & ((1 << depth) - 1)

        row = []
        for x in range(width):
            if ctype == 3:
                row.append(tuple(palette[sample(x) * 3:sample(x) * 3 + 3]))
            elif ctype == 0:
                v = sample(x) * 255 // ((1 << depth) - 1)
                row.append((v, v, v))
            elif ctype == 4:
                row.append((line[x * 2],) * 3)
            else:
                row.append(tuple(line[x * channels:x * channels + 3]))
        rows.append(row)
    return width, height, rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    levels = int(sys.argv[sys.argv.index("--levels") + 1]) if "--levels" in sys.argv else 256
    if "--levels" in sys.argv:
        args.remove(sys.argv[sys.argv.index("--levels") + 1])
    if len(args) != 2:
        sys.exit(__doc__)
    wa, ha, a = load(args[0])
    wb, hb, b = load(args[1])
    if (wa, ha) != (wb, hb):
        print(f"sizes differ: {wa}x{ha} and {wb}x{hb}")
        sys.exit(1)

    def level(p):
        return tuple(round(v * (levels - 1) / 255) for v in p)

    bad = [(x, y) for y in range(ha) for x in range(wa) if level(a[y][x]) != level(b[y][x])]
    if bad:
        print(f"{len(bad)} pixels differ; the first: {bad[:8]}")
        sys.exit(1)
    print("same")


if __name__ == "__main__":
    main()
