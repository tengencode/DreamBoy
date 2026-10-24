#!/usr/bin/env python3
"""Measure a 16-bit stereo WAV: loudness and pitch per time window, plus a waveform picture.

usage: wav_probe.py <file.wav> [--window SECONDS] [--png out.png]
Prints the duration, then one line per window:
  <start>-<end> s  rms L <left> R <right>  peak <hz> Hz
The peak (strongest frequency of left+right, found with an FFT and parabolic interpolation)
is left out for nearly silent windows. --png draws the whole file: left channel on top, right
below, one column per time slice (min..max of the samples in it).
"""
import argparse
import cmath
import math
import struct
import sys
import wave
import zlib


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 2:
            sys.exit("expected 16-bit stereo")
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    data = struct.unpack("<%dh" % (len(raw) // 2), raw)
    return rate, data[0::2], data[1::2]


def fft(values):
    """Iterative radix-2 FFT; len(values) must be a power of two."""
    n = len(values)
    a = [complex(v) for v in values]
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        half = size // 2
        twiddles = [cmath.exp(-2j * math.pi * k / size) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                u = a[start + k]
                v = a[start + k + half] * twiddles[k]
                a[start + k] = u + v
                a[start + k + half] = u - v
        size <<= 1
    return a


def peak_hz(samples, rate):
    n = 1
    while n * 2 <= min(len(samples), 16384):
        n *= 2
    if n < 256:
        return None
    mean = sum(samples[:n]) / n
    windowed = [(samples[i] - mean) * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) for i in range(n)]
    mags = [abs(c) for c in fft(windowed)[: n // 2]]
    low = max(2, int(20 * n / rate))  # ignore everything below 20 Hz
    k = max(range(low, n // 2 - 1), key=lambda i: mags[i])
    a, b, c = mags[k - 1], mags[k], mags[k + 1]
    denom = a - 2 * b + c
    shift = 0.5 * (a - c) / denom if denom else 0.0
    shift = max(-0.5, min(0.5, shift))  # only meaningful at a true local maximum
    return (k + shift) * rate / n


def rms(values):
    return math.sqrt(sum(v * v for v in values) / len(values)) if values else 0.0


def write_png(path, width, height, pixels):
    rows = b"".join(b"\0" + bytes(pixels[y * width:(y + 1) * width]) for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)  # 8-bit greyscale
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def draw(path, left, right):
    width, lane = 800, 100
    height = lane * 2
    pixels = bytearray(width * height)
    for ch, samples in enumerate((left, right)):
        top = ch * lane
        for x in range(width):
            part = samples[len(samples) * x // width: max(len(samples) * (x + 1) // width, len(samples) * x // width + 1)]
            if not part:
                continue
            lo = top + int((32768 - max(part)) * (lane - 1) / 65535)
            hi = top + int((32768 - min(part)) * (lane - 1) / 65535)
            for y in range(lo, hi + 1):
                pixels[y * width + x] = 255
        for x in range(width):
            pixels[(top + lane // 2) * width + x] |= 80  # the zero line
    write_png(path, width, height, pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wav")
    parser.add_argument("--window", type=float, default=0.25, help="window length in seconds (default 0.25)")
    parser.add_argument("--png", help="write a waveform picture of the whole file")
    args = parser.parse_args()

    rate, left, right = load(args.wav)
    print(f"duration {len(left) / rate:.3f} s, {rate} Hz")
    step = max(1, int(args.window * rate))
    for start in range(0, len(left) - step + 1, step):
        l_part, r_part = left[start:start + step], right[start:start + step]
        line = f"{start / rate:6.2f}-{(start + step) / rate:6.2f} s  rms L {rms(l_part):8.1f} R {rms(r_part):8.1f}"
        mono = [(a + b) / 2 for a, b in zip(l_part, r_part)]
        spread = max(mono) - min(mono)
        if spread > 400:
            hz = peak_hz(mono, rate)
            if hz:
                line += f"  peak {hz:7.1f} Hz"
        print(line)
    if args.png:
        draw(args.png, left, right)
        print(f"wrote {args.png}")


if __name__ == "__main__":
    main()
