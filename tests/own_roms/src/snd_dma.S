@ snd_dma: a 512 Hz triangle (32-byte period) at 16,384 samples per second through
@   0-1 s FIFO A (timer 0 + DMA1), then 1-2 s FIFO B (timer 1 + DMA2). Then silence.
    .include "common.inc"
    rom_start
    io16 0x084, 0x0080      @ sound on
    io16 0x080, 0x0000      @ PSG muted

    io16 0x082, 0x0B04      @ A: 100%, left + right, timer 0, FIFO reset
    io32 0x0BC, triangle    @ DMA1 source
    io32 0x0C0, 0x040000A0  @ DMA1 destination: FIFO A
    io16 0x0C6, 0xB640      @ DMA1: enable, sound FIFO timing, 32-bit, repeat, fixed destination
    io16 0x100, 0xFC00      @ timer 0: overflow every 1024 cycles = 16,384 Hz
    io16 0x102, 0x0080
    frames 60
    io16 0x102, 0x0000
    io16 0x0C6, 0x0000

    io16 0x082, 0xF008      @ B: 100%, left + right, timer 1, FIFO reset (A off)
    io32 0x0C8, triangle    @ DMA2 source
    io32 0x0CC, 0x040000A4  @ DMA2 destination: FIFO B
    io16 0x0D2, 0xB640
    io16 0x104, 0xFC00      @ timer 1
    io16 0x106, 0x0080
    frames 60
    io16 0x106, 0x0000
    io16 0x0D2, 0x0000
    io16 0x084, 0x0000
done:
    b done
    .ltorg
    .align 2
triangle:                   @ 544 periods = 17,408 bytes: a little more than one second
    .rept 544
    .byte 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70
    .byte 0x7F, 0x6F, 0x5F, 0x4F, 0x3F, 0x2F, 0x1F, 0x0F, 0xFF, 0xEF, 0xDF, 0xCF, 0xBF, 0xAF, 0x9F, 0x8F
    .endr
