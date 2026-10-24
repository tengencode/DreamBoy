@ EEPROM test shared by save_eeprom512.S and save_eeprom8k.S. Define ADDR_BITS (6 or 14) and
@ LAST_BLOCK (63 or 1023) before including this file (after common.inc).
@ r12 = the first failing check (0 = pass); r12 is not the I/O base here, so io16/io32 are not used.
@   1-3 ready bit after writing blocks 1, 2, LAST_BLOCK   4-6 those blocks read back
@   7 block 3 (never written) reads all 0xFF              8 ready bit after writing the run marker
@ Screen (backdrop): green = pass on a blank chip, blue = pass and block 0 already held the run
@ marker (a save from an earlier run was loaded), red = a check failed.

    .equ BUF, 0x02000000   @ outgoing bits, one halfword each
    .equ REPLY, 0x02000400 @ incoming bits
    .equ MARK_HI, 0x52554E21
    .equ MARK_LO, 0x0BADF00D

    .macro expect_ready n
    ldr r0, =0x0D000000
    ldrh r0, [r0]
    tst r0, #1
    moveq r12, #\n
    beq report
    .endm

    .macro write_block block, hi, lo, n
    ldr r8, =\block
    ldr r9, =\hi
    ldr r10, =\lo
    bl ee_write
    expect_ready \n
    .endm

    .macro expect_block block, hi, lo, n
    ldr r8, =\block
    bl ee_read
    ldr r0, =\hi
    cmp r9, r0
    ldreq r0, =\lo
    cmpeq r10, r0
    movne r12, #\n
    bne report
    .endm

    rom_start
    mov r12, #0
    mov r11, #0 @ becomes 1 when the run marker is found
    mov r8, #0
    bl ee_read  @ block 0: the run marker?
    ldr r0, =MARK_HI
    cmp r9, r0
    ldreq r0, =MARK_LO
    cmpeq r10, r0
    moveq r11, #1

    write_block 1, 0x01234567, 0x89ABCDEF, 1
    write_block 2, 0xDEADBEEF, 0x00FF00FF, 2
    write_block LAST_BLOCK, 0xA5A5A5A5, 0x5A5A5A5A, 3
    expect_block 1, 0x01234567, 0x89ABCDEF, 4
    expect_block 2, 0xDEADBEEF, 0x00FF00FF, 5
    expect_block LAST_BLOCK, 0xA5A5A5A5, 0x5A5A5A5A, 6
    expect_block 3, 0xFFFFFFFF, 0xFFFFFFFF, 7
    write_block 0, MARK_HI, MARK_LO, 8

report:
    ldr r1, =0x03E0     @ green
    cmp r11, #1
    ldreq r1, =0x7C00   @ blue: an earlier run's save was loaded
    cmp r12, #0
    ldrne r1, =0x001F   @ red: a check failed
    ldr r0, =0x05000000
    strh r1, [r0]       @ backdrop colour
    frames 2            @ let whole frames show it
done:
    b done

@ put_bits: append the low r1 bits of r0, most significant first, as halfwords at r4 (r4 advances).
put_bits:
    subs r1, r1, #1
    bxmi lr
1:  mov r2, r0, lsr r1
    and r2, r2, #1
    strh r2, [r4], #2
    subs r1, r1, #1
    bpl 1b
    bx lr

@ dma3: copy r7 halfwords from r5 to r6; it runs before the next instruction.
dma3:
    ldr r2, =0x040000D4
    str r5, [r2]
    str r6, [r2, #4]
    orr r3, r7, #0x80000000 @ enable, 16-bit, immediate, both addresses increment
    str r3, [r2, #8]
    bx lr

@ ee_write: write r9:r10 (8 bytes, r9 first) to block r8.
ee_write:
    stmfd sp!, {lr}
    ldr r4, =BUF
    mov r0, #2          @ "1 0": write
    mov r1, #2
    bl put_bits
    mov r0, r8
    mov r1, #ADDR_BITS
    bl put_bits
    mov r0, r9
    mov r1, #32
    bl put_bits
    mov r0, r10
    mov r1, #32
    bl put_bits
    mov r0, #0          @ stop bit
    mov r1, #1
    bl put_bits
    ldr r5, =BUF
    ldr r6, =0x0D000000
    mov r7, #(ADDR_BITS + 67)
    bl dma3
    ldmfd sp!, {pc}

@ ee_read: read block r8 into r9:r10.
ee_read:
    stmfd sp!, {lr}
    ldr r4, =BUF
    mov r0, #3          @ "1 1": read
    mov r1, #2
    bl put_bits
    mov r0, r8
    mov r1, #ADDR_BITS
    bl put_bits
    mov r0, #0
    mov r1, #1
    bl put_bits
    ldr r5, =BUF
    ldr r6, =0x0D000000
    mov r7, #(ADDR_BITS + 3)
    bl dma3
    ldr r5, =0x0D000000
    ldr r6, =REPLY
    mov r7, #68
    bl dma3
    ldr r4, =REPLY + 8  @ skip the 4 dummy bits
    mov r9, #0
    mov r10, #0
    mov r1, #32
1:  ldrh r2, [r4], #2
    and r2, r2, #1
    orr r9, r2, r9, lsl #1
    subs r1, r1, #1
    bne 1b
    mov r1, #32
2:  ldrh r2, [r4], #2
    and r2, r2, #1
    orr r10, r2, r10, lsl #1
    subs r1, r1, #1
    bne 2b
    ldmfd sp!, {pc}

    .ltorg
    .align 2
    .ascii "EEPROM_V124"      @ the save library's marker: this cartridge has an EEPROM
    .byte 0
