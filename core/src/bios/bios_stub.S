@ HLE BIOS stub, assembled by tools/gen_bios_stub.py into bios_stub.inc.
@
@ Only what games can observe mirrors the retail BIOS:
@   0x0E4        word the BIOS read latch holds after boot
@   0x128-0x13C  IRQ handler (same instructions and addresses)
@   0x140-0x188  SWI entry/exit (0x144 and 0x188 match), 0x190 latch word after SWIs
@ SWIs other than IntrWait/VBlankIntrWait trap into C via the undefined
@ instruction 0xE7F000F0 with the SWI number in r12.

        .syntax unified
        .arm
        .text
        .global _start
_start:
        b       reset_vector            @ 0x00 Reset
        b       undef_vector            @ 0x04 Undefined instruction
        b       swi_vector              @ 0x08 SWI
        b       abort_vector            @ 0x0C Prefetch abort
        b       abort_vector            @ 0x10 Data abort
        b       abort_vector            @ 0x14 Reserved
        b       irq_vector              @ 0x18 IRQ
        b       abort_vector            @ 0x1C FIQ

reset_vector:
        mov     r12, #0                 @ SoftReset
        .word   0xE7F000F0
undef_vector:
        movs    pc, lr
abort_vector:
        subs    pc, lr, #4

        .org    0x0E4
        .word   0xE129F000

        .org    0x128
irq_vector:
        stmfd   sp!, {r0-r3, r12, lr}
        mov     r0, #0x04000000
        add     lr, pc, #0
        ldr     pc, [r0, #-4]           @ game handler at 0x03FFFFFC (mirror of 0x03007FFC)
        ldmfd   sp!, {r0-r3, r12, lr}
        subs    pc, lr, #4

swi_vector:                             @ 0x140
        stmfd   sp!, {r11, r12, lr}
        ldrb    r12, [lr, #-2]          @ SWI number: comment byte (ARM bits 16-23 / Thumb bits 0-7)
        mrs     r11, spsr
        stmfd   sp!, {r11}
        and     r11, r11, #0x80         @ keep the caller's IRQ-disable bit
        orr     r11, r11, #0x1F         @ run in System mode, like the real BIOS
        msr     cpsr_fc, r11
        stmfd   sp!, {r2, lr}
        bl      swi_dispatch
        ldmfd   sp!, {r2, lr}
        mov     r12, #0xD3
        msr     cpsr_fc, r12
        ldmfd   sp!, {r11}
        msr     spsr_fc, r11
        ldmfd   sp!, {r11, r12, lr}
        .org    0x188                   @ the zero padding above decodes as andeq r0, r0, r0 (a no-op)
        movs    pc, lr
        .org    0x190
        .word   0xE3A02004

        .org    0x200
swi_dispatch:
        cmp     r12, #0x04
        beq     intr_wait
        cmp     r12, #0x05
        beq     vblank_intr_wait
        .word   0xE7F000F0              @ C executes SWI r12
        bx      lr

vblank_intr_wait:
        mov     r0, #1
        mov     r1, #1
intr_wait:                              @ r0: discard old flags, r1: IRQ flags to wait for
        stmfd   sp!, {r2, r3, lr}
        mov     r3, #0x04000000
        mov     r2, #1
        strb    r2, [r3, #0x208]        @ IME = 1
        cmp     r0, #0
        ldrhne  r2, [r3, #-8]           @ BIOS_IF at 0x03FFFFF8 (mirror of 0x03007FF8)
        bicne   r2, r2, r1
        strhne  r2, [r3, #-8]
1:
        ldrh    r2, [r3, #-8]
        ands    r0, r2, r1
        bne     2f
        mov     r2, #0
        strb    r2, [r3, #0x301]        @ HALTCNT: sleep until an interrupt
        b       1b
2:
        bic     r2, r2, r1
        strh    r2, [r3, #-8]
        ldmfd   sp!, {r2, r3, pc}
