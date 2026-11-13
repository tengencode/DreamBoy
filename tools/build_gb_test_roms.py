#!/usr/bin/env python3
"""Build our own Game Boy test ROMs from bytes: tests/own_roms/gb_demo.gb and gbc_demo.gbc.

One small program, written here as machine code with labels (no assembler needed): a striped background that
scrolls one pixel a frame, a square-wave tone that changes about once a second, and a counter in cartridge RAM that
counts those seconds (so there is a save to keep). gb_demo runs as a plain Game Boy (MBC1 + RAM + battery, four
greys); gbc_demo carries the colour flag (MBC5 + RAM + battery, a four-colour palette).
The Nintendo logo area stays blank. Re-run after editing and commit the regenerated files.
"""
from pathlib import Path

OUT_DIR = Path(__file__).resolve().parent.parent / "tests/own_roms"


class Code:
    """Machine code at an address, with labels for jumps."""

    def __init__(self, origin):
        self.origin, self.data, self.labels, self.fixups = origin, bytearray(), {}, []

    def here(self):
        return self.origin + len(self.data)

    def label(self, name):
        self.labels[name] = self.here()

    def op(self, *values):
        self.data += bytes(values)

    def jr(self, opcode, target):  # a relative jump
        self.data += bytes([opcode, 0])
        self.fixups.append(("rel", len(self.data) - 1, target))

    def word(self, opcode, target):  # an opcode followed by a label's address
        self.data += bytes([opcode, 0, 0])
        self.fixups.append(("abs", len(self.data) - 2, target))

    def done(self):
        for kind, at, target in self.fixups:
            addr = self.labels[target]
            if kind == "abs":
                self.data[at:at + 2] = addr.to_bytes(2, "little")
            else:
                offset = addr - (self.origin + at + 1)
                assert -128 <= offset <= 127, target
                self.data[at] = offset & 0xFF
        return bytes(self.data)


def program():
    c = Code(0x150)
    c.op(0xF3)                      # di
    c.op(0x31, 0xFE, 0xFF)          # ld sp,$FFFE
    c.label("wait")                 # wait for VBlank, then switch the screen off
    c.op(0xF0, 0x44, 0xFE, 0x90)    # ldh a,(LY); cp 144
    c.jr(0x38, "wait")              # jr c,wait
    c.op(0xAF, 0xE0, 0x40)          # xor a; ldh (LCDC),a
    c.op(0x21, 0x10, 0x80, 0x06, 0x08)  # ld hl,$8010 (tile 1); ld b,8
    c.label("tile")                 # every row: colours 3 2 1 0 3 2 1 0
    c.op(0x3E, 0xAA, 0x22, 0x3E, 0xCC, 0x22, 0x05)  # ld a,$AA; ld (hl+),a; ld a,$CC; ld (hl+),a; dec b
    c.jr(0x20, "tile")
    c.op(0x21, 0x00, 0x98, 0x01, 0x00, 0x04)  # ld hl,$9800; ld bc,$0400
    c.label("map")                  # a checkerboard of tiles 0 and 1
    c.op(0x7D, 0xAC, 0xE6, 0x01, 0x22, 0x0B, 0x78, 0xB1)  # ld a,l; xor h; and 1; ld (hl+),a; dec bc; ld a,b; or c
    c.jr(0x20, "map")
    c.op(0x3E, 0xE4, 0xE0, 0x47)    # BGP: colour n = shade n
    c.op(0x3E, 0x80, 0xE0, 0x68)    # BCPS: palette 0, moving on (a plain Game Boy ignores these)
    c.word(0x21, "palette")         # ld hl,palette
    c.op(0x06, 0x08)                # ld b,8
    c.label("pal")
    c.op(0x2A, 0xE0, 0x69, 0x05)    # ld a,(hl+); ldh (BCPD),a; dec b
    c.jr(0x20, "pal")
    for reg, value in ((0x26, 0x80), (0x25, 0xFF), (0x24, 0x77), (0x16, 0x80), (0x17, 0xF0), (0x18, 0xD6), (0x19, 0x86)):
        c.op(0x3E, value, 0xE0, reg)  # sound on; square 2 at 440 Hz, full volume, both sides
    c.op(0x3E, 0x0A, 0xEA, 0x00, 0x00)  # cartridge RAM on
    c.op(0x3E, 0x91, 0xE0, 0x40)    # screen on
    c.op(0x3E, 0x01, 0xE0, 0xFF, 0xFB)  # VBlank interrupt on; ei
    c.label("main")
    c.op(0x76, 0x00)                # halt; nop
    c.jr(0x18, "main")
    c.label("vblank")               # once a frame
    c.op(0xF5, 0xE5)                # push af; push hl
    c.op(0xF0, 0x43, 0x3C, 0xE0, 0x43)  # SCX + 1
    c.op(0x21, 0x00, 0xC0, 0x34, 0x7E, 0xE6, 0x3F)  # ld hl,$C000; inc (hl); ld a,(hl); and $3F
    c.jr(0x20, "skip")              # every 64 frames:
    c.op(0x21, 0x00, 0xA0, 0x34)    #   the counter in cartridge RAM + 1
    c.op(0x7E, 0x07, 0x07, 0x07, 0xE0, 0x18)  #   and a new pitch from it: ld a,(hl); rlca x3; ldh (NR23),a
    c.label("skip")
    c.op(0xE1, 0xF1, 0xD9)          # pop hl; pop af; reti
    c.label("palette")              # white, red, green, blue (15-bit, red lowest)
    c.op(0xFF, 0x7F, 0x1F, 0x00, 0xE0, 0x03, 0x00, 0x7C)
    return c


def build(name, title, cart_type, cgb_flag):
    rom = bytearray(0x8000)
    c = program()
    rom[0x150:0x150 + len(c.done())] = c.done()
    rom[0x40:0x43] = bytes([0xC3]) + c.labels["vblank"].to_bytes(2, "little")  # the VBlank vector: jp vblank
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop; jp $0150
    rom[0x134:0x134 + len(title)] = title
    rom[0x143] = cgb_flag
    rom[0x147] = cart_type
    rom[0x148] = 0x00  # 32 KiB
    rom[0x149] = 0x02  # 8 KiB of cartridge RAM
    rom[0x14D] = (-(sum(rom[0x134:0x14D]) + 0x19)) & 0xFF
    out = OUT_DIR / name
    out.write_bytes(rom)
    print(f"wrote tests/own_roms/{name} ({len(rom)} bytes)")


if __name__ == "__main__":
    build("gb_demo.gb", b"GB DEMO", 0x03, 0x00)
    build("gbc_demo.gbc", b"GBC DEMO", 0x1B, 0x80)
