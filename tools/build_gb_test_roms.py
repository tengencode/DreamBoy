#!/usr/bin/env python3
"""Build our own Game Boy test ROMs from bytes: tests/own_roms/gb_demo.gb, gbc_demo.gbc and gb_tilt.gb.

One small program, written here as machine code with labels (no assembler needed): a striped background that
scrolls one pixel a frame, a square-wave tone that changes about once a second, and a counter in cartridge RAM that
counts those seconds (so there is a save to keep). gb_demo runs as a plain Game Boy (MBC1 + RAM + battery, four
greys); gbc_demo carries the colour flag (MBC5 + RAM + battery, a four-colour palette).
gb_tilt is a tilt cartridge (MBC7): see tilt_program.
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


def tilt_program():
    """gb_tilt: a ball (one sprite) that sits where the cartridge's tilt sensor says, read once a frame; START writes
    a word to the save chip, and a square in the top left corner shows that the word was found there at start."""
    PINS = 0xA080  # the save chip: bit 7 chip select, bit 6 clock, bit 1 data in, bit 0 data out
    c = Code(0x150)

    def pins(value):
        c.op(0x3E, value, 0xEA, PINS & 0xFF, PINS >> 8)  # ld a,value; ld (PINS),a

    def command(bits):  # straight-line code, a clock pulse per bit: no loops to get wrong
        pins(0x00)
        pins(0x80)
        for bit in bits:
            pins(0x80 | bit << 1)
            pins(0xC0 | bit << 1)

    def bits_of(value, count):
        return [(value >> i) & 1 for i in range(count - 1, -1, -1)]

    c.op(0xF3)                      # di
    c.op(0x31, 0xFE, 0xFF)          # ld sp,$FFFE
    c.label("wait")                 # wait for VBlank, then switch the screen off
    c.op(0xF0, 0x44, 0xFE, 0x90)    # ldh a,(LY); cp 144
    c.jr(0x38, "wait")
    c.op(0xAF, 0xE0, 0x40)          # xor a; ldh (LCDC),a
    c.op(0x21, 0x10, 0x80, 0x06, 0x10, 0x3E, 0xFF)  # ld hl,$8010 (tile 1); ld b,16; ld a,$FF: a black square
    c.label("tile")
    c.op(0x22, 0x05)                # ld (hl+),a; dec b
    c.jr(0x20, "tile")
    c.op(0x21, 0x00, 0x98, 0x01, 0x00, 0x04)  # ld hl,$9800; ld bc,$0400: an empty background
    c.label("map")
    c.op(0xAF, 0x22, 0x0B, 0x78, 0xB1)  # xor a; ld (hl+),a; dec bc; ld a,b; or c
    c.jr(0x20, "map")
    c.op(0x21, 0x00, 0xFE, 0x06, 0xA0)  # ld hl,$FE00; ld b,160: no sprites
    c.label("oam")
    c.op(0xAF, 0x22, 0x05)          # xor a; ld (hl+),a; dec b
    c.jr(0x20, "oam")
    c.op(0x3E, 0x01, 0xEA, 0x02, 0xFE)  # sprite 0 is tile 1
    c.op(0x3E, 0xE4, 0xE0, 0x47, 0xE0, 0x48)  # BGP and OBP0: colour n = shade n
    c.op(0x3E, 0x0A, 0xEA, 0x00, 0x00)  # the chip's two switches
    c.op(0x3E, 0x40, 0xEA, 0x00, 0x40)
    command([1, 1, 0] + bits_of(0, 8))  # read word 0; its high byte, bit by bit, into L
    c.op(0x2E, 0x00)                # ld l,0
    for _ in range(8):
        pins(0x80)
        pins(0xC0)
        c.op(0xFA, PINS & 0xFF, PINS >> 8, 0x1F, 0xCB, 0x15)  # ld a,(PINS); rra; rl l
    pins(0x00)
    c.op(0x7D, 0xFE, 0x5A)          # ld a,l; cp $5A
    c.jr(0x20, "fresh")
    c.op(0x3E, 0x01, 0xEA, 0x00, 0x98)  # the saved word is there: a square in the corner
    c.label("fresh")
    c.op(0x3E, 0x93, 0xE0, 0x40)    # screen on, with sprites
    c.label("main")
    c.op(0xF0, 0x44, 0xFE, 0x90)    # wait for VBlank
    c.jr(0x20, "main")
    c.op(0x3E, 0x55, 0xEA, 0x00, 0xA0)  # erase, then latch: a new reading
    c.op(0x3E, 0xAA, 0xEA, 0x10, 0xA0)
    # A reading is 0x81D0 less the tilt (0x70 = 1 g), so its low byte less 0x60 is 0x70 - tilt: 0 .. 0xE0.
    for low, oam, centre in ((0x20, 0x01, 84), (0x40, 0x00, 84)):  # X -> the sprite's x, Y -> its y
        c.op(0xFA, low, 0xA0, 0xD6, 0x60, 0x47)  # ld a,(reading); sub $60; ld b,a
        c.op(0x3E, 0x70, 0x90, 0xCB, 0x2F)       # ld a,$70; sub b; sra a: half the tilt, signed
        c.op(0xC6, centre, 0xEA, oam, 0xFE)      # add centre; ld (OAM),a
    c.op(0x3E, 0x10, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xCB, 0x5F)  # the buttons: bit 3,a (START, 0 = held)
    c.word(0xC2, "nosave")          # jp nz,nosave
    command([1, 0, 0, 1, 1] + bits_of(0, 6))  # write enable
    command([1, 0, 1] + bits_of(0, 8) + bits_of(0x5AA5, 16))  # write word 0
    pins(0x00)
    c.op(0x3E, 0x01, 0xEA, 0x00, 0x98)  # and show it
    c.label("nosave")
    c.label("leave")                # wait for the end of this VBlank line
    c.op(0xF0, 0x44, 0xFE, 0x90)
    c.jr(0x28, "leave")
    c.word(0xC3, "main")
    return c


def build(name, title, cart_type, cgb_flag, program=program, ram_code=0x02):
    rom = bytearray(0x8000)
    c = program()
    rom[0x150:0x150 + len(c.done())] = c.done()
    if "vblank" in c.labels:  # the VBlank vector: jp vblank
        rom[0x40:0x43] = bytes([0xC3]) + c.labels["vblank"].to_bytes(2, "little")
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop; jp $0150
    rom[0x134:0x134 + len(title)] = title
    rom[0x143] = cgb_flag
    rom[0x147] = cart_type
    rom[0x148] = 0x00  # 32 KiB
    rom[0x149] = ram_code  # 0x02: 8 KiB of cartridge RAM
    rom[0x14D] = (-(sum(rom[0x134:0x14D]) + 0x19)) & 0xFF
    out = OUT_DIR / name
    out.write_bytes(rom)
    print(f"wrote tests/own_roms/{name} ({len(rom)} bytes)")


if __name__ == "__main__":
    build("gb_demo.gb", b"GB DEMO", 0x03, 0x00)
    build("gbc_demo.gbc", b"GBC DEMO", 0x1B, 0x80)
    build("gb_tilt.gb", b"TILT TEST", 0x22, 0x00, tilt_program, 0x00)
