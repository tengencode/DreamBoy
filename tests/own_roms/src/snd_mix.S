@ snd_mix: mixer settings, 30 frames (about half a second) each:
@   0.0 square 2 (439.8 Hz) left only; 0.5 right only; 1.0 both at PSG 50%; 1.5 both at PSG 25%;
@   2.0 FIFO A triangle (512 Hz) at 50%; 2.5 the same at 100%. Then silence.
    .include "common.inc"
    rom_start
    io16 0x084, 0x0080      @ sound on
    io16 0x082, 0x0002      @ PSG 100%
    io16 0x068, 0xF080      @ square 2: volume 15, 50% duty
    io16 0x080, 0x2077      @ square 2 on the left only, volume 7
    io16 0x06C, 0x86D6      @ trigger, frequency 1750
    frames 30
    io16 0x080, 0x0277      @ right only
    frames 30
    io16 0x080, 0x2277      @ both sides
    io16 0x082, 0x0001      @ PSG 50%
    frames 30
    io16 0x082, 0x0000      @ PSG 25%
    frames 30
    io16 0x068, 0x0000      @ square 2 off

    io16 0x082, 0x0B00      @ A: 50%, left + right, timer 0, FIFO reset
    io32 0x0BC, triangle
    io32 0x0C0, 0x040000A0
    io16 0x0C6, 0xB640
    io16 0x100, 0xFC00
    io16 0x102, 0x0080
    frames 30
    io16 0x082, 0x0304      @ A: 100% (no reset: keeps playing)
    frames 30
    io16 0x102, 0x0000
    io16 0x0C6, 0x0000
    io16 0x084, 0x0000
done:
    b done
    .ltorg
    .align 2
triangle:
    .rept 544
    .byte 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70
    .byte 0x7F, 0x6F, 0x5F, 0x4F, 0x3F, 0x2F, 0x1F, 0x0F, 0xFF, 0xEF, 0xDF, 0xCF, 0xBF, 0xAF, 0x9F, 0x8F
    .endr
