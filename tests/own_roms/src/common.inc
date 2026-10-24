@ Shared by our own test ROMs. The HLE boot enters at 0x08000000 in ARM state, System mode,
@ with interrupts off. r12 holds 0x04000000 (the I/O registers) after rom_start.

    .syntax unified
    .arm

@ io16 OFF, VALUE: halfword to the I/O register at 0x04000000 + OFF (uses r0, r1)
    .macro io16 off, value
    ldr r0, =\value
    ldr r1, =\off
    strh r0, [r12, r1]
    .endm

@ io32 OFF, VALUE: word to the I/O register at 0x04000000 + OFF (uses r0, r1)
    .macro io32 off, value
    ldr r0, =\value
    ldr r1, =\off
    str r0, [r12, r1]
    .endm

@ frames N: wait until N more VBlanks have started (uses r0-r2, lr)
    .macro frames n
    ldr r0, =\n
    bl wait_frames
    .endm

@ rom_start: entry branch, header space and the wait_frames helper; code continues after it.
    .macro rom_start
    .text
    .global _start
_start:
    b main
    .space 0xC0 - 4 @ cartridge header, filled in by tools/build_test_roms.py
wait_frames:
    ldr r2, =0x04000006 @ VCOUNT
1:  ldrh r1, [r2]
    cmp r1, #160
    beq 1b              @ leave the current VBlank line first
2:  ldrh r1, [r2]
    cmp r1, #160
    bne 2b              @ then wait for the next VBlank to start
    subs r0, r0, #1
    bne 1b
    bx lr
main:
    ldr r12, =0x04000000
    .endm
