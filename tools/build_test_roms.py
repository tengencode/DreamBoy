#!/usr/bin/env python3
"""Assemble our own test ROMs: tests/own_roms/src/<name>.S -> tests/own_roms/<name>.gba.

Uses clang, ld.lld and llvm-objcopy from the Android NDK (or from $CLANG_BIN_DIR), like
tools/gen_bios_stub.py. Fills the cartridge header (title, game code, maker, fixed byte and
complement checksum); the Nintendo logo area stays blank, so these ROMs are for HLE mode.
Re-run after editing a source and commit the regenerated .gba files.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

from gen_bios_stub import find_bin_dir, tool  # the NDK's clang, ld.lld and llvm-objcopy

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "tests/own_roms/src"
OUT_DIR = ROOT / "tests/own_roms"


def game_code(src: Path) -> bytes:
    """A line `@ game code: XXXX` in the source picks the game code (the core's list of games with extras matches its
    first three letters); otherwise ZOWN."""
    for line in src.read_text(encoding="utf-8").splitlines():
        if line.startswith("@ game code:"):
            code = line.split(":", 1)[1].strip().encode("ascii")
            if len(code) != 4:
                sys.exit(f"{src.name}: a game code has 4 letters")
            return code
    return b"ZOWN"


def fill_header(rom: bytearray, name: str, code: bytes) -> None:
    rom[0xA0:0xAC] = name.upper().replace("_", " ")[:12].encode("ascii").ljust(12, b"\0")
    rom[0xAC:0xB0] = code     # game code
    rom[0xB0:0xB2] = b"00"    # maker
    rom[0xB2] = 0x96          # fixed value
    rom[0xBD] = (-(sum(rom[0xA0:0xBD]) + 0x19)) & 0xFF


def build(src: Path, bin_dir: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        elf = Path(tmp) / "rom.elf"
        raw = Path(tmp) / "rom.bin"
        subprocess.run(
            [tool(bin_dir, "clang"), "--target=armv4t-none-eabi", "-nostdlib", "-fuse-ld=lld", "-I", str(SRC_DIR),
             "-Wl,-Ttext=0x08000000", "-Wl,--entry=_start", str(src), "-o", str(elf)],
            check=True)
        subprocess.run(
            [tool(bin_dir, "llvm-objcopy"), "-O", "binary", "--only-section=.text", str(elf), str(raw)],
            check=True)
        rom = bytearray(raw.read_bytes())
    if len(rom) < 0xC0:
        sys.exit(f"{src.name}: too small to hold a header")
    fill_header(rom, src.stem, game_code(src))
    rom += b"\0" * (-len(rom) % 4)
    out = OUT_DIR / (src.stem + ".gba")
    out.write_bytes(rom)
    print(f"wrote {out.relative_to(ROOT).as_posix()} ({len(rom)} bytes)")


def main() -> None:
    bin_dir = find_bin_dir()
    sources = sorted(SRC_DIR.glob("*.S"))
    if not sources:
        sys.exit(f"no sources in {SRC_DIR}")
    for src in sources:
        build(src, bin_dir)


if __name__ == "__main__":
    main()
