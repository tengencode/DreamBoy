@ snd_psg: each PSG channel plays for 60 frames (about one second).
@   0-1 s square 1 at 439.8 Hz; 1-2 s square 2 at 879.7 Hz; 2-3 s wave channel sine at 439.8 Hz;
@   3-4 s noise; 4-5 s square 1 with a slow upward sweep from 131 Hz. Then silence.
    .include "common.inc"
    rom_start
    io16 0x084, 0x0080      @ SOUNDCNT_X: sound on
    io16 0x080, 0xFF77      @ SOUNDCNT_L: all PSG channels left + right, volume 7
    io16 0x082, 0x0002      @ SOUNDCNT_H: PSG 100%

    io16 0x060, 0x0000      @ square 1: no sweep
    io16 0x062, 0xF080      @ volume 15, envelope off, 50% duty
    io16 0x064, 0x86D6      @ trigger, frequency 1750 → 131072 / 298 = 439.8 Hz
    frames 60
    io16 0x062, 0x0000      @ DAC off: silent

    io16 0x068, 0xF080      @ square 2
    io16 0x06C, 0x876B      @ trigger, frequency 1899 → 131072 / 149 = 879.7 Hz
    frames 60
    io16 0x068, 0x0000

    io16 0x070, 0x0040      @ bank 1 selected for playback, so the CPU writes bank 0
    ldr r3, =sine
    ldr r4, =0x04000090
    ldmia r3, {r5-r8}
    stmia r4, {r5-r8}
    io16 0x070, 0x0080      @ play bank 0, DAC on
    io16 0x072, 0x2000      @ volume 100%
    io16 0x074, 0x876B      @ trigger, frequency 1899 → 65536 / 149 = 439.8 Hz
    frames 60
    io16 0x070, 0x0000

    io16 0x078, 0xF000      @ noise: volume 15
    io16 0x07C, 0x8021      @ trigger, 15-bit, r 1, s 2: a step every 256 cycles
    frames 60
    io16 0x078, 0x0000

    io16 0x060, 0x0077      @ sweep time 7, up, shift 7
    io16 0x062, 0xF080
    io16 0x064, 0x83E8      @ trigger, frequency 1000 → 125.1 Hz, rising
    frames 60
    io16 0x062, 0x0000
    io16 0x084, 0x0000      @ sound off
done:
    b done
    .ltorg
    .align 2
sine:                       @ 32 four-bit samples, round(7.5 + 7.5 sin(2 pi i / 32)), high nibble first
    .byte 0x89, 0xAC, 0xDE, 0xEF, 0xFF, 0xEE, 0xDC, 0xA9, 0x86, 0x53, 0x21, 0x10, 0x00, 0x11, 0x23, 0x56
