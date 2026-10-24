/*
 * The ARM64 assembly CPU loop (milestone 7b); its state and the C it calls are in cpu_a64.c, its plain-C twin is
 * cpu_run_generic (cpu.c, cpu_fast.h). Each handler runs one guest instruction and jumps straight to the next
 * one's handler. Anything a handler does not take goes to a64_arm_slow (cpu_step, in C) before any side effect.
 *
 * Registers while the loop runs (callee-saved, so they survive the calls into C):
 *   x19 ctx (a64_ctx_t)        w20 guest PC (the instruction to run)   x21 now (cycles)     x22 limit
 *   x23 guest registers r0-r15, CPSR at +64                            x24 &bus.waitcnt
 *   x25 the code window's host address    w26 next fetch sequential    w27 window start     w28 window length
 * Within a handler: w10 the opcode. x18 is never used (Android's shadow call stack).
 */

#define CTX_LIMIT 8
#define CTX_WIN_HOST 16
#define CTX_WIN_LO 24
#define CTX_FETCH_N 32
#define CTX_FETCH_S 33
#define CTX_STALL 34
#define CTX_ROM_SIZE 36
#define CTX_EWRAM 40
#define CTX_IWRAM 48
#define CTX_ROM 56
#define CTX_NOW 64
#define CTX_PAL 80
#define CTX_VRAM 88
#define CTX_OAM 96
#define CTX_STALL_S 75
#define R_LR 56
#define R_PC 60
#define R_CPSR 64
#define R_NEXT_SEQ 181
#define BUS_CYCLES 2
#define BUS_IN_BIOS 66
#define BUS_OFF_ROM 76
#define BUS_PREFETCH_END 80
#define BUS_OPEN_BUS 84

    .text
    .hidden cpu_cond_table
    .hidden a64_arm_table
    .hidden a64_thumb_table

/* State between the registers and memory (r15, now, next_fetch_seq; limit and window from ctx). */
.macro STORE_STATE
    str w20, [x23, #R_PC]
    ldr x9, [x19, #CTX_NOW]
    str x21, [x9]
    strb w26, [x23, #R_NEXT_SEQ]
.endm

.macro LOAD_STATE
    ldr w20, [x23, #R_PC]
    ldr x9, [x19, #CTX_NOW]
    ldr x21, [x9]
    ldrb w26, [x23, #R_NEXT_SEQ]
    ldr x22, [x19, #CTX_LIMIT]
    ldr x25, [x19, #CTX_WIN_HOST]
    ldp w27, w28, [x19, #CTX_WIN_LO]
.endm

/* The ARM instruction at w20: limit, window, condition, then its handler. */
.macro ARM_DISPATCH
    cmp x21, x22
    b.hs .Larm_exit
    sub w9, w20, w27
    cmp w9, w28
    b.hs .Larm_window
    ldr w10, [x25, w9, uxtw]
    lsr w13, w10, #28
    cmp w13, #14
    b.eq 99f /* AL: no flags to look at */
    ldr w11, [x23, #R_CPSR]
    adrp x12, cpu_cond_table
    add x12, x12, :lo12:cpu_cond_table
    ldrh w12, [x12, w13, uxtw #1]
    lsr w11, w11, #28
    lsr w12, w12, w11
    tst w12, #1
    b.eq .Larm_skip
99:
    ubfx w11, w10, #20, #8
    ubfx w12, w10, #4, #4
    orr w11, w12, w11, lsl #4
    adrp x12, a64_arm_table
    add x12, x12, :lo12:a64_arm_table
    ldr x12, [x12, w11, uxtw #3]
    br x12
.endm

/* w9 = the cycles of this instruction's fetch. */
.macro FETCH_CYCLES
    ldrb w9, [x19, #CTX_FETCH_N]
    ldrb w11, [x19, #CTX_FETCH_S]
    cmp w26, #0
    csel w9, w11, w9, ne
.endm

/* The fetch counted (w9 its cycles), as bus_fetch32 and cpu_step do it; r15 = PC + 8. */
.macro ARM_FETCHED_W9
    add x21, x21, x9
    strb wzr, [x24, #BUS_IN_BIOS]
    str w10, [x24, #BUS_OPEN_BUS]
    add w9, w20, #8
    str w9, [x23, #R_PC]
.endm

.macro ARM_FETCHED
    FETCH_CYCLES
    ARM_FETCHED_W9
.endm

/* \dst = the cycles of an access to region \reg (0-255): bus.cycles[region][wide][seq], 1 above region 15. */
.macro CYCLES dst, reg, wide, seq, tmp
    mov \dst, #1
    cmp \reg, #15
    b.hi 77f
    add \tmp, x24, \reg, uxtw #2
    ldrb \dst, [\tmp, #(BUS_CYCLES + \wide * 2 + \seq)]
77:
.endm

/* After a taken branch to w20: the refill (N at the target, S after it), the prefetch buffer emptied. */
.macro REFILL size, wide
    lsr w1, w20, #24
    CYCLES w2, w1, \wide, 0, x3
    add w4, w20, #\size
    lsr w4, w4, #24
    CYCLES w5, w4, \wide, 1, x3
    add x21, x21, x2
    add x21, x21, x5
    str wzr, [x24, #BUS_PREFETCH_END]
.endm

/* N and Z from w0; C and V kept. */
.macro FLAGS_NZ
    ldr w7, [x23, #R_CPSR]
    and w7, w7, #0x3fffffff
    and w6, w0, #0x80000000
    orr w7, w7, w6
    cmp w0, #0
    cset w6, eq
    orr w7, w7, w6, lsl #30
    str w7, [x23, #R_CPSR]
.endm

/* bus_prefetch_stall in the loop (the window is the cartridge and the buffer is on): w11 = the cycles the bus was
 * off the cartridge, w20 = the next instruction. Credits the cycles the buffer saves; the next fetch is sequential. */
.macro STALL
    ldrb w9, [x19, #CTX_STALL_S] /* the cartridge's sequential wait states (16-bit) */
    ldr w13, [x24, #BUS_PREFETCH_END]
    sub w13, w13, w20 /* halfwords still buffered from an earlier stall */
    mov w14, #0
    mov w15, #8
    cmp w13, #16
    b.hs 41f
    lsr w14, w13, #1
    sub w15, w15, w14
41:
    add w16, w9, #1 /* stall */
    mov w17, #1 /* loads */
42:
    cmp w16, w11
    b.ge 43f
    cmp w17, w15
    b.ge 43f
    add w16, w16, w9
    add w17, w17, #1
    b 42b
43:
    add w13, w17, w14
    sub w13, w13, #1
    add w13, w20, w13, lsl #1
    str w13, [x24, #BUS_PREFETCH_END]
    cmp w16, w11
    csel w13, w16, w11, gt /* the wait cannot be shorter than the prefetch */
    sub w13, w13, w11
    sub w13, w13, w16
    add x21, x21, w13, sxtw
    mov w26, #1
.endm

/* After a data access (w11 the cycles off the cartridge bus, w12 the data's region; w20 already the next
 * instruction): the next fetch is non-sequential, and cartridge code may stall for the prefetch buffer. */
.macro AFTER_DATA dispatch
    mov w26, #0
    cmp w12, #8
    b.hs 91f
    ldrb w9, [x19, #CTX_STALL]
    cbz w9, 91f
    STALL
91:
    \dispatch
.endm

/* x8 = the host address of the data at w6 (aligned to \size): EWRAM, IWRAM, the palette, VRAM, OAM (not byte
 * stores there), or for a load the ROM itself (not its first 0x100 bytes, not past its end); w12 = its region.
 * Anything else goes to \slow (fast_data_ptr's rules). */
.macro DATA_PTR store, size, slow
    lsr w12, w6, #24
    cmp w12, #3
    b.eq 82f
    cmp w12, #2
    b.eq 81f
    sub w13, w12, #5
    cmp w13, #2
    b.ls 86f
.if \store
    b \slow
.else
    sub w13, w12, #8
    cmp w13, #4
    b.hi \slow
    and w13, w6, #0x1ffffff
    cmp w13, #0x100
    b.lo \slow
    ldr w14, [x19, #CTX_ROM_SIZE]
    add w15, w13, #\size
    cmp w15, w14
    b.hi \slow
    ldr x8, [x19, #CTX_ROM]
    add x8, x8, w13, uxtw
    b 83f
.endif
86: /* the palette, VRAM, OAM */
.if \store && (\size == 1)
    b \slow
.else
    cmp w12, #6
    b.eq 87f
    b.hi 88f
    ldr x8, [x19, #CTX_PAL]
    and w13, w6, #0x3ff
    add x8, x8, w13, uxtw
    b 83f
87: /* 96 KiB in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF */
    ldr x8, [x19, #CTX_VRAM]
    and w13, w6, #0x1ffff
    sub w14, w13, #0x8000
    cmp w13, #0x18, lsl #12
    csel w13, w14, w13, hs
    add x8, x8, w13, uxtw
    b 83f
88:
    ldr x8, [x19, #CTX_OAM]
    and w13, w6, #0x3ff
    add x8, x8, w13, uxtw
    b 83f
.endif
81:
    ldr x8, [x19, #CTX_EWRAM]
    and w13, w6, #0x3ffff
    add x8, x8, w13, uxtw
    b 83f
82:
    ldr x8, [x19, #CTX_IWRAM]
    and w13, w6, #0x7fff
    add x8, x8, w13, uxtw
83:
.endm

/* ---- the loop ---- */

    .global a64_run_arm
    .type a64_run_arm, %function
/* int a64_run_arm(a64_ctx_t *x, uint32_t *regs, uint8_t *bus): the loop in ARM state, from r15. Returns 2 after a
 * BX to Thumb, else 0. */
a64_run_arm:
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov x19, x0
    mov x23, x1
    mov x24, x2
    LOAD_STATE
    ARM_DISPATCH

.Larm_exit:
    STORE_STATE
    mov w0, #0
.Larm_return:
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #96
    ret

/* BX to Thumb: the state is stored; the caller goes on in the Thumb loop (2: no cpu_step in between). */
.Larm_switch:
    STORE_STATE
    mov w0, #2
    b .Larm_return

/* PC left the code window: find the new one, or run the instruction with cpu_step. */
.Larm_window:
    mov x0, x19
    mov w1, w20
    mov w2, #4
    bl a64_window
    cbz w0, a64_arm_slow
    ldr x25, [x19, #CTX_WIN_HOST]
    ldp w27, w28, [x19, #CTX_WIN_LO]
    ARM_DISPATCH

/* Any instruction the loop does not take (before any side effect): cpu_step runs it. */
    .global a64_arm_slow
    .type a64_arm_slow, %function
a64_arm_slow:
    STORE_STATE
    mov x0, x19
    bl a64_slow
    cbz w0, .Larm_return /* cpu_step has stored everything */
    LOAD_STATE
    ARM_DISPATCH

/* The condition failed: only the fetch. */
.Larm_skip:
    ARM_FETCHED
    add w20, w20, #4
    mov w26, #1
    ARM_DISPATCH

/* ---- data processing: a64_arm_dp_<opcode>_<S>_<kind>, kind 0 immediate, 1-4 LSL/LSR/ASR/ROR by an immediate ---- */

/* w4 = the C flag before the instruction. */
.macro OLD_C
    ldr w4, [x23, #R_CPSR]
    ubfx w4, w4, #29, #1
.endm

/* w3 = w3 shifted by the immediate amount w6 (shift_by_imm's rules: LSL #0 none, LSR/ASR #0 = #32, ROR #0 = RRX);
 * with \needc the carry-out into w4. \kind: 1 LSL, 2 LSR, 3 ASR, 4 ROR. */
.macro SHIFT_IMM kind, needc
.if \kind == 1 /* LSL: #0 = no shift */
  .if \needc
    OLD_C
    cbz w6, 72f
    mov w7, #32
    sub w7, w7, w6
    lsr w4, w3, w7
    and w4, w4, #1
    lsl w3, w3, w6
72:
  .else
    lsl w3, w3, w6
  .endif
.elseif \kind == 2 /* LSR: #0 = #32 */
    cbnz w6, 73f
  .if \needc
    lsr w4, w3, #31
  .endif
    mov w3, #0
    b 74f
73:
  .if \needc
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
  .endif
    lsr w3, w3, w6
74:
.elseif \kind == 3 /* ASR: #0 = #32 */
    cbnz w6, 73f
  .if \needc
    lsr w4, w3, #31
  .endif
    asr w3, w3, #31
    b 74f
73:
  .if \needc
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
  .endif
    asr w3, w3, w6
74:
.else /* ROR: #0 = RRX */
    cbnz w6, 73f
    ldr w7, [x23, #R_CPSR]
    ubfx w7, w7, #29, #1
  .if \needc
    and w4, w3, #1
  .endif
    lsr w3, w3, #1
    orr w3, w3, w7, lsl #31
    b 74f
73:
  .if \needc
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
  .endif
    ror w3, w3, w6
74:
.endif
.endm

/* Operand 2 into w3; with \needc the shifter's carry-out into w4. */
.macro OPERAND kind, needc
.if \kind == 0
    and w3, w10, #0xff
    ubfx w5, w10, #8, #4
    lsl w5, w5, #1
    ror w3, w3, w5
  .if \needc
    OLD_C
    cbz w5, 71f
    lsr w4, w3, #31
71:
  .endif
.else
    and w5, w10, #0xf
    ldr w3, [x23, w5, uxtw #2]
    ubfx w6, w10, #7, #5
    SHIFT_IMM \kind, \needc
.endif
.endm

/* NZCV from the host flags into CPSR. */
.macro FLAGS_ARITH
    mrs x6, nzcv
    ldr w7, [x23, #R_CPSR]
    lsr w6, w6, #28
    bfi w7, w6, #28, #4
    str w7, [x23, #R_CPSR]
.endm

/* N and Z from w0, C from w4, V kept. */
.macro FLAGS_LOGIC
    ldr w7, [x23, #R_CPSR]
    and w7, w7, #0x1fffffff
    and w6, w0, #0x80000000
    orr w7, w7, w6
    cmp w0, #0
    cset w6, eq
    orr w7, w7, w6, lsl #30
    orr w7, w7, w4, lsl #29
    str w7, [x23, #R_CPSR]
.endm

/* The guest C flag into the host's (for ADC / SBC / RSC). */
.macro HOST_C
    ldr w6, [x23, #R_CPSR]
    msr nzcv, x6
.endm

.macro DP opc, s, kind
    .type a64_arm_dp_\opc\()_\s\()_\kind, %function
a64_arm_dp_\opc\()_\s\()_\kind:
    ubfx w1, w10, #12, #4
    cmp w1, #15
    b.eq a64_arm_slow
    ARM_FETCHED
    ubfx w2, w10, #16, #4
    ldr w2, [x23, w2, uxtw #2]
    .set logical, (\opc == 0) || (\opc == 1) || (\opc == 8) || (\opc == 9) || (\opc >= 12)
    OPERAND \kind, (\s && logical)
.if (\opc == 0) || (\opc == 8)
    and w0, w2, w3
.elseif (\opc == 1) || (\opc == 9)
    eor w0, w2, w3
.elseif (\opc == 2) || (\opc == 10)
  .if \s
    subs w0, w2, w3
  .else
    sub w0, w2, w3
  .endif
.elseif \opc == 3
  .if \s
    subs w0, w3, w2
  .else
    sub w0, w3, w2
  .endif
.elseif (\opc == 4) || (\opc == 11)
  .if \s
    adds w0, w2, w3
  .else
    add w0, w2, w3
  .endif
.elseif \opc == 5
    HOST_C
  .if \s
    adcs w0, w2, w3
  .else
    adc w0, w2, w3
  .endif
.elseif \opc == 6
    HOST_C
  .if \s
    sbcs w0, w2, w3
  .else
    sbc w0, w2, w3
  .endif
.elseif \opc == 7
    HOST_C
  .if \s
    sbcs w0, w3, w2
  .else
    sbc w0, w3, w2
  .endif
.elseif \opc == 12
    orr w0, w2, w3
.elseif \opc == 13
    mov w0, w3
.elseif \opc == 14
    bic w0, w2, w3
.else
    mvn w0, w3
.endif
.if \s
  .if logical
    FLAGS_LOGIC
  .else
    FLAGS_ARITH
  .endif
.endif
.if (\opc < 8) || (\opc > 11)
    str w0, [x23, w1, uxtw #2]
.endif
    add w20, w20, #4
    mov w26, #1
    ARM_DISPATCH
.endm

.irp opc, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
  .irp s, 0,1
    .if ((\opc < 8) || (\opc > 11)) || \s
      .irp kind, 0,1,2,3,4
        DP \opc, \s, \kind
      .endr
    .endif
  .endr
.endr

/* ---- B / BL ---- */
    .global a64_arm_branch
    .type a64_arm_branch, %function
a64_arm_branch:
    ARM_FETCHED
    sbfx w1, w10, #0, #24
    tbz w10, #24, 75f
    add w2, w20, #4
    str w2, [x23, #R_LR]
75:
    add w20, w20, #8
    add w20, w20, w1, lsl #2
    REFILL 4, 1
    mov w26, #1
    ARM_DISPATCH

/* ---- LDR / STR / LDRB / STRB with memory DATA_PTR takes; not PC as the destination ---- */

/* !P or W: Rn = the moved base (w4). */
.macro WRITEBACK
    tst w10, #(1 << 24)
    b.eq 97f
    tst w10, #(1 << 21)
    b.eq 98f
97:
    ubfx w9, w10, #16, #4
    str w4, [x23, w9, uxtw #2]
98:
.endm

/* The rest of one: w5 the address, w4 the moved base, w1 Rd; \wb: Rn = the moved base. */
.macro ARM_SINGLE_TAIL load, byte, wb
.if \byte
    mov w6, w5
    DATA_PTR (1 - \load), 1, a64_arm_slow
.else
    and w6, w5, #~3
    DATA_PTR (1 - \load), 4, a64_arm_slow
.endif
    add x13, x24, w12, uxtw #2
    ldrb w13, [x13, #(BUS_CYCLES + (1 - \byte) * 2)]
    FETCH_CYCLES
    add x14, x21, x9
    cmp x14, x22
    b.hs a64_arm_slow /* a DMA may be due by the access: the bus runs it */
    ARM_FETCHED_W9
    add x21, x21, x13
    cmp w12, #8
    csel w11, w13, wzr, lo /* off_rom: the data access, when not the cartridge */
.if \load
  .if \byte
    ldrb w0, [x8]
  .else
    ldr w0, [x8]
    and w9, w5, #3
    lsl w9, w9, #3
    ror w0, w0, w9
  .endif
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
  .if \wb
    ubfx w9, w10, #16, #4
    str w4, [x23, w9, uxtw #2]
  .endif
    str w0, [x23, w1, uxtw #2] /* a load into the base register wins */
.else
    ldr w0, [x23, w1, uxtw #2]
  .if \byte
    strb w0, [x8]
  .else
    str w0, [x8]
  .endif
  .if \wb
    ubfx w9, w10, #16, #4
    str w4, [x23, w9, uxtw #2]
  .endif
.endif
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH
.endm

/* One copy per I P U B W L (instruction bits 25-20): a64_arm_sgl_<bits 27-20>. */
.macro SINGLE hi
a64_arm_sgl_\hi:
    ubfx w1, w10, #12, #4
    cmp w1, #15
    b.eq a64_arm_slow
    add w9, w20, #8
    str w9, [x23, #R_PC] /* the base or the offset register may be PC */
.if (\hi >> 5) & 1 /* offset = Rm shifted by an immediate (shift_by_imm; its carry-out is unused) */
    and w5, w10, #0xf
    ldr w3, [x23, w5, uxtw #2]
    ubfx w6, w10, #7, #5
    ubfx w7, w10, #5, #2
    cbz w7, 63f
    cmp w7, #2
    b.lo 64f
    b.eq 65f
    cbnz w6, 66f /* ROR #0 = RRX */
    ldr w7, [x23, #R_CPSR]
    ubfx w7, w7, #29, #1
    lsr w3, w3, #1
    orr w3, w3, w7, lsl #31
    b 62f
66:
    ror w3, w3, w6
    b 62f
63:
    lsl w3, w3, w6
    b 62f
64: /* LSR: #0 = #32 */
    lsr w3, w3, w6
    cbnz w6, 62f
    mov w3, #0
    b 62f
65: /* ASR: #0 = #32 */
    cbnz w6, 67f
    mov w6, #31
67:
    asr w3, w3, w6
62:
.else
    and w3, w10, #0xfff
.endif
    ubfx w2, w10, #16, #4
    ldr w2, [x23, w2, uxtw #2] /* base */
.if (\hi >> 3) & 1
    add w4, w2, w3 /* moved */
.else
    sub w4, w2, w3
.endif
.if (\hi >> 4) & 1
    mov w5, w4 /* pre-indexed */
.else
    mov w5, w2
.endif
    ARM_SINGLE_TAIL (\hi & 1), ((\hi >> 2) & 1), ((((\hi >> 4) & 1) == 0) || ((\hi >> 1) & 1))
.endm

.irp hi, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
    SINGLE \hi
.endr

/* ==== Thumb state ==== */

/* The Thumb instruction at w20: limit, window, then its handler. */
.macro THUMB_DISPATCH
    cmp x21, x22
    b.hs .Lthumb_exit
    sub w9, w20, w27
    cmp w9, w28
    b.hs .Lthumb_window
    ldrh w10, [x25, w9, uxtw]
    lsr w11, w10, #6
    adrp x12, a64_thumb_table
    add x12, x12, :lo12:a64_thumb_table
    ldr x12, [x12, w11, uxtw #3]
    br x12
.endm

/* The fetch counted (w9 its cycles), as bus_fetch16 and cpu_step do it; r15 = PC + 4. */
.macro THUMB_FETCHED_W9
    add x21, x21, x9
    strb wzr, [x24, #BUS_IN_BIOS]
    orr w11, w10, w10, lsl #16
    str w11, [x24, #BUS_OPEN_BUS]
    add w11, w20, #4
    str w11, [x23, #R_PC]
.endm

.macro THUMB_FETCHED
    FETCH_CYCLES
    THUMB_FETCHED_W9
.endm

/* The next instruction, after one without a data access or a branch. */
.macro THUMB_NEXT
    add w20, w20, #2
    mov w26, #1
    THUMB_DISPATCH
.endm




/* One Thumb load or store: w5 the address, w1 the register. Kinds: 0 STR, 1 STRB, 2 STRH, 3 LDR, 4 LDRB, 5 LDRH,
 * 6 LDSB, 7 LDSH (aligned; a misaligned LDSH is an LDSB, see TMEM). */
.macro TMEM_BODY kind, size
.if \size == 4
    and w6, w5, #~3
.elseif \size == 2
    and w6, w5, #~1
.else
    mov w6, w5
.endif
    DATA_PTR (\kind <= 2), \size, a64_thumb_slow
    add x13, x24, w12, uxtw #2
.if \size == 4
    ldrb w13, [x13, #(BUS_CYCLES + 2)]
.else
    ldrb w13, [x13, #BUS_CYCLES]
.endif
    FETCH_CYCLES
    add x14, x21, x9
    cmp x14, x22
    b.hs a64_thumb_slow /* a DMA may be due by the access: the bus runs it */
    THUMB_FETCHED_W9
    add x21, x21, x13
    cmp w12, #8
    csel w11, w13, wzr, lo
.if \kind == 0
    ldr w0, [x23, w1, uxtw #2]
    str w0, [x8]
.elseif \kind == 1
    ldr w0, [x23, w1, uxtw #2]
    strb w0, [x8]
.elseif \kind == 2
    ldr w0, [x23, w1, uxtw #2]
    strh w0, [x8]
.else
  .if \kind == 3
    ldr w0, [x8]
    and w9, w5, #3
    lsl w9, w9, #3
    ror w0, w0, w9
  .elseif \kind == 4
    ldrb w0, [x8]
  .elseif \kind == 5
    ldrh w0, [x8]
    and w9, w5, #1
    lsl w9, w9, #3
    ror w0, w0, w9
  .elseif \kind == 6
    ldrsb w0, [x8]
  .else
    ldrsh w0, [x8]
  .endif
    str w0, [x23, w1, uxtw #2]
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
.endif
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH
.endm

.macro TMEM kind
.if \kind == 7
    tst w5, #1
    b.ne 92f
    TMEM_BODY 7, 2
92:
    TMEM_BODY 6, 1
.elseif \kind == 0 || \kind == 3
    TMEM_BODY \kind, 4
.elseif \kind == 2 || \kind == 5
    TMEM_BODY \kind, 2
.else
    TMEM_BODY \kind, 1
.endif
.endm

/* w3 = w3 shifted by register amount w6 (shift_by_reg); w4 = the carry (unchanged for amount 0). \t: 0 LSL,
 * 1 LSR, 2 ASR, 3 ROR. */
.macro SHIFT_REG t
    OLD_C
    cbz w6, 96f
.if \t == 0
    cmp w6, #32
    b.hi 93f
    b.eq 94f
    mov w7, #32
    sub w7, w7, w6
    lsr w4, w3, w7
    and w4, w4, #1
    lsl w3, w3, w6
    b 96f
94:
    and w4, w3, #1
    mov w3, #0
    b 96f
93:
    mov w4, #0
    mov w3, #0
.elseif \t == 1
    cmp w6, #32
    b.hi 93f
    b.eq 94f
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
    lsr w3, w3, w6
    b 96f
94:
    lsr w4, w3, #31
    mov w3, #0
    b 96f
93:
    mov w4, #0
    mov w3, #0
.elseif \t == 2
    cmp w6, #32
    b.hs 93f
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
    asr w3, w3, w6
    b 96f
93:
    lsr w4, w3, #31
    asr w3, w3, #31
.else
    and w6, w6, #31
    cbnz w6, 93f
    lsr w4, w3, #31
    b 96f
93:
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
    ror w3, w3, w6
.endif
96:
.endm

    .text
    .global a64_run_thumb
    .type a64_run_thumb, %function
/* void a64_run_thumb(a64_ctx_t *x, uint32_t *regs, uint8_t *bus): the loop in Thumb state, from r15. */
a64_run_thumb:
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov x19, x0
    mov x23, x1
    mov x24, x2
    LOAD_STATE
    THUMB_DISPATCH

.Lthumb_exit:
    STORE_STATE
    mov w0, #0
.Lthumb_return:
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #96
    ret

.Lthumb_switch:
    STORE_STATE
    mov w0, #2
    b .Lthumb_return

.Lthumb_window:
    mov x0, x19
    mov w1, w20
    mov w2, #2
    bl a64_window
    cbz w0, a64_thumb_slow
    ldr x25, [x19, #CTX_WIN_HOST]
    ldp w27, w28, [x19, #CTX_WIN_LO]
    THUMB_DISPATCH

    .global a64_thumb_slow
    .type a64_thumb_slow, %function
a64_thumb_slow:
    STORE_STATE
    mov x0, x19
    bl a64_slow
    cbz w0, .Lthumb_return
    LOAD_STATE
    THUMB_DISPATCH

/* THUMB 1: LSL / LSR / ASR Rd, Rs, #imm5 */
.macro T_SHIFT t
a64_thumb_shift_\t:
    THUMB_FETCHED
    ubfx w5, w10, #3, #3
    ldr w3, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #5
    SHIFT_IMM (1 + \t), 1
    mov w0, w3
    and w1, w10, #7
    str w0, [x23, w1, uxtw #2]
    FLAGS_LOGIC
    THUMB_NEXT
.endm

/* THUMB 2: ADD / SUB Rd, Rs, Rn | #imm3 */
.macro T_ADDSUB v
a64_thumb_addsub_\v:
    THUMB_FETCHED
    ubfx w5, w10, #3, #3
    ldr w2, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #3
.if \v & 2
    mov w3, w6
.else
    ldr w3, [x23, w6, uxtw #2]
.endif
.if \v & 1
    subs w0, w2, w3
.else
    adds w0, w2, w3
.endif
    FLAGS_ARITH
    and w1, w10, #7
    str w0, [x23, w1, uxtw #2]
    THUMB_NEXT
.endm

/* THUMB 3: MOV / CMP / ADD / SUB Rd, #imm8 */
.macro T_IMM v
a64_thumb_imm_\v:
    THUMB_FETCHED
    ubfx w1, w10, #8, #3
    and w3, w10, #0xff
.if \v == 0
    mov w0, w3
    str w0, [x23, w1, uxtw #2]
    FLAGS_NZ
.else
    ldr w2, [x23, w1, uxtw #2]
  .if \v == 2
    adds w0, w2, w3
  .else
    subs w0, w2, w3
  .endif
    FLAGS_ARITH
  .if \v != 1
    str w0, [x23, w1, uxtw #2]
  .endif
.endif
    THUMB_NEXT
.endm

/* THUMB 4: ALU operations (not MUL, which cpu_step runs) */
.macro T_ALU v
a64_thumb_alu_\v:
    THUMB_FETCHED
    and w1, w10, #7
    ldr w2, [x23, w1, uxtw #2]
    ubfx w5, w10, #3, #3
    ldr w3, [x23, w5, uxtw #2]
.if \v == 0 || \v == 8
    and w0, w2, w3
    FLAGS_NZ
.elseif \v == 1
    eor w0, w2, w3
    FLAGS_NZ
.elseif \v == 2 || \v == 3 || \v == 4 || \v == 7
    and w6, w3, #0xff
    mov w3, w2
  .if \v == 7
    SHIFT_REG 3
  .else
    SHIFT_REG (\v - 2)
  .endif
    mov w0, w3
    FLAGS_LOGIC
    add x21, x21, #1 /* bus_idle(1) */
.elseif \v == 5
    HOST_C
    adcs w0, w2, w3
    FLAGS_ARITH
.elseif \v == 6
    HOST_C
    sbcs w0, w2, w3
    FLAGS_ARITH
.elseif \v == 9
    negs w0, w3
    FLAGS_ARITH
.elseif \v == 10
    subs w0, w2, w3
    FLAGS_ARITH
.elseif \v == 11
    adds w0, w2, w3
    FLAGS_ARITH
.elseif \v == 12
    orr w0, w2, w3
    FLAGS_NZ
.elseif \v == 14
    bic w0, w2, w3
    FLAGS_NZ
.else
    mvn w0, w3
    FLAGS_NZ
.endif
.if \v != 8 && \v != 10 && \v != 11
    str w0, [x23, w1, uxtw #2]
.endif
    THUMB_NEXT
.endm

/* THUMB 5: ADD / CMP / MOV with high registers, not writing PC */
.macro T_HI v
a64_thumb_hi_\v:
    and w1, w10, #7
    lsr w5, w10, #4
    and w5, w5, #8
    orr w1, w1, w5 /* rd: H1 is bit 7 */
.if \v != 1
    cmp w1, #15
    b.eq a64_thumb_slow
.endif
    THUMB_FETCHED
    ubfx w5, w10, #3, #4
    ldr w3, [x23, w5, uxtw #2]
.if \v == 0
    ldr w2, [x23, w1, uxtw #2]
    add w0, w2, w3
    str w0, [x23, w1, uxtw #2]
.elseif \v == 1
    ldr w2, [x23, w1, uxtw #2]
    subs w0, w2, w3
    FLAGS_ARITH
.else
    str w3, [x23, w1, uxtw #2]
.endif
    THUMB_NEXT
.endm

/* THUMB 6: LDR Rd, [PC, #imm8*4] */
a64_thumb_pcload:
    add w5, w20, #4
    and w5, w5, #~2
    and w6, w10, #0xff
    add w5, w5, w6, lsl #2
    ubfx w1, w10, #8, #3
    TMEM 3

/* THUMB 7 / 8: [Rb, Ro] */
.macro T_REGOFF name, kind
a64_thumb_\name:
    ubfx w5, w10, #3, #3
    ldr w5, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #3
    ldr w6, [x23, w6, uxtw #2]
    add w5, w5, w6
    and w1, w10, #7
    TMEM \kind
.endm

/* THUMB 9 / 10: [Rb, #imm5 * scale] */
.macro T_IMMOFF name, kind, shift
a64_thumb_\name:
    ubfx w5, w10, #3, #3
    ldr w5, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #5
    add w5, w5, w6, lsl #\shift
    and w1, w10, #7
    TMEM \kind
.endm

/* THUMB 11: [SP, #imm8*4] */
.macro T_SPREL name, kind
a64_thumb_\name:
    ldr w5, [x23, #52]
    and w6, w10, #0xff
    add w5, w5, w6, lsl #2
    ubfx w1, w10, #8, #3
    TMEM \kind
.endm

/* THUMB 12: ADD Rd, PC | SP, #imm8*4 */
.macro T_LDADDR v
a64_thumb_ldaddr_\v:
    THUMB_FETCHED
.if \v
    ldr w2, [x23, #52]
.else
    add w2, w20, #4
    and w2, w2, #~2
.endif
    and w6, w10, #0xff
    add w0, w2, w6, lsl #2
    ubfx w1, w10, #8, #3
    str w0, [x23, w1, uxtw #2]
    THUMB_NEXT
.endm

/* THUMB 13: ADD SP, #±imm7*4 */
a64_thumb_spadj:
    THUMB_FETCHED
    ldr w2, [x23, #52]
    and w6, w10, #0x7f
    lsl w6, w6, #2
    add w3, w2, w6
    sub w4, w2, w6
    tst w10, #0x80
    csel w2, w4, w3, ne
    str w2, [x23, #52]
    THUMB_NEXT

/* THUMB 16: B<cond> */
a64_thumb_bcond:
    THUMB_FETCHED
    ldr w11, [x23, #R_CPSR]
    adrp x12, cpu_cond_table
    add x12, x12, :lo12:cpu_cond_table
    ubfx w13, w10, #8, #4
    ldrh w12, [x12, w13, uxtw #1]
    lsr w11, w11, #28
    lsr w12, w12, w11
    tst w12, #1
    b.ne 1f
    THUMB_NEXT
1:
    sxtb w1, w10
    add w20, w20, #4
    add w20, w20, w1, lsl #1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH

/* THUMB 18: B */
a64_thumb_b:
    THUMB_FETCHED
    sbfx w1, w10, #0, #11
    add w20, w20, #4
    add w20, w20, w1, lsl #1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH

/* THUMB 19: BL, first half: LR = PC + (offset11 << 12) */
a64_thumb_blhi:
    THUMB_FETCHED
    sbfx w1, w10, #0, #11
    add w2, w20, #4
    add w2, w2, w1, lsl #12
    str w2, [x23, #R_LR]
    THUMB_NEXT

/* THUMB 19, second half: to LR + (offset11 << 1); LR = the next instruction | 1 */
a64_thumb_bllo:
    THUMB_FETCHED
    ldr w2, [x23, #R_LR]
    and w1, w10, #0x7ff
    add w2, w2, w1, lsl #1
    add w3, w20, #2
    orr w3, w3, #1
    str w3, [x23, #R_LR]
    and w20, w2, #~1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH

.irp t, 0,1,2
    T_SHIFT \t
.endr
.irp v, 0,1,2,3
    T_ADDSUB \v
    T_IMM \v
.endr
.irp v, 0,1,2,3,4,5,6,7,8,9,10,11,12,14,15
    T_ALU \v
.endr
.irp v, 0,1,2
    T_HI \v
.endr
    T_REGOFF regoff_0, 0
    T_REGOFF regoff_1, 1
    T_REGOFF regoff_2, 3
    T_REGOFF regoff_3, 4
    T_REGOFF regoffs_0, 2
    T_REGOFF regoffs_1, 6
    T_REGOFF regoffs_2, 5
    T_REGOFF regoffs_3, 7
    T_IMMOFF immoff_0, 0, 2
    T_IMMOFF immoff_1, 3, 2
    T_IMMOFF immoff_2, 1, 0
    T_IMMOFF immoff_3, 4, 0
    T_IMMOFF half_0, 2, 1
    T_IMMOFF half_1, 5, 1
    T_SPREL sprel_0, 0
    T_SPREL sprel_1, 3
    T_LDADDR 0
    T_LDADDR 1

/* ==== multi-register transfers with RAM (fast_block_ptr's rules) ==== */

/* w3 = the number of bits set in w2 (up to 16 bits). */
.macro POPCOUNT16
    fmov s0, w2
    cnt v0.8b, v0.8b
    addv b0, v0.8b
    fmov w3, s0
.endm

/* A block of w3 words from w5 (each aligned down) in EWRAM or IWRAM, all in one region, whose accesses take as
 * long sequential as not: x8 the region's memory, w13 its address mask, w14 the cycles per word, w12 the region.
 * Anything else goes to \slow. */
.macro BLOCK_CHECK slow
    and w16, w5, #~3
    lsr w12, w16, #24
    sub w17, w3, #1
    add w17, w16, w17, lsl #2
    lsr w17, w17, #24
    cmp w17, w12
    b.ne \slow
    cmp w12, #2
    b.eq 84f
    cmp w12, #3
    b.ne \slow
    ldr x8, [x19, #CTX_IWRAM]
    mov w13, #0x7fff
    b 85f
84:
    ldr x8, [x19, #CTX_EWRAM]
    mov w13, #0x3ffff
85:
    add x16, x24, w12, uxtw #2
    ldrb w14, [x16, #(BUS_CYCLES + 2)]
    ldrb w17, [x16, #(BUS_CYCLES + 3)]
    cmp w14, w17
    b.ne \slow
.endm

/* w9 = the fetch's cycles; no DMA may be due by the last word: now + fetch + (count - 1) * cycles < limit. */
.macro BLOCK_TIME slow
    FETCH_CYCLES
    sub w17, w3, #1
    mul w17, w17, w14
    add x16, x21, x9
    add x16, x16, x17
    cmp x16, x22
    b.hs \slow
.endm

/* One word at w5 (then w5 += 4), its cycles off the cartridge bus. */
.macro BLK_LOAD dst
    and w16, w5, #~3
    and w16, w16, w13
    ldr \dst, [x8, w16, uxtw]
    add x21, x21, x14
    add w11, w11, w14
    add w5, w5, #4
.endm

.macro BLK_STORE src
    and w16, w5, #~3
    and w16, w16, w13
    str \src, [x8, w16, uxtw]
    add x21, x21, x14
    add w11, w11, w14
    add w5, w5, #4
.endm

/* w15 = the lowest set bit of w2, which is cleared. */
.macro NEXT_BIT
    rbit w15, w2
    clz w15, w15
    sub w17, w2, #1
    and w2, w2, w17
.endm

/* THUMB 14: PUSH {rlist, LR} / POP {rlist, PC}, not an empty list */
a64_thumb_pushpop:
    and w2, w10, #0xff
    POPCOUNT16
    ubfx w4, w10, #8, #1
    add w3, w3, w4
    cbz w3, a64_thumb_slow
    ldr w5, [x23, #52]
    tst w10, #0x800
    b.ne 1f
    sub w5, w5, w3, lsl #2 /* PUSH: from SP - 4 * count */
1:
    mov w6, w5
    BLOCK_CHECK a64_thumb_slow
    BLOCK_TIME a64_thumb_slow
    THUMB_FETCHED_W9
    mov w11, #0
    tst w10, #0x800
    b.eq 3f
2: /* POP */
    cbz w2, 4f
    NEXT_BIT
    BLK_LOAD w0
    str w0, [x23, w15, uxtw #2]
    b 2b
4:
    tst w10, #0x100
    b.eq 5f
    BLK_LOAD w0 /* POP {pc}: ARMv4 does not change state */
    str w5, [x23, #52]
    add x21, x21, #1 /* bus_idle(1) */
    and w20, w0, #~1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH
5:
    str w5, [x23, #52]
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH
3: /* PUSH */
    str w6, [x23, #52]
6:
    cbz w2, 7f
    NEXT_BIT
    ldr w0, [x23, w15, uxtw #2]
    BLK_STORE w0
    b 6b
7:
    tst w10, #0x100
    b.eq 8f
    ldr w0, [x23, #R_LR]
    BLK_STORE w0
8:
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH

/* THUMB 15: STMIA / LDMIA Rb!, {rlist}, not an empty list */
a64_thumb_block:
    and w2, w10, #0xff
    POPCOUNT16
    cbz w3, a64_thumb_slow
    ubfx w1, w10, #8, #3
    ldr w4, [x23, w1, uxtw #2]
    mov w5, w4
    BLOCK_CHECK a64_thumb_slow
    BLOCK_TIME a64_thumb_slow
    THUMB_FETCHED_W9
    mov w11, #0
    add w6, w4, w3, lsl #2 /* the new base */
    tst w10, #0x800
    b.eq 3f
    str w6, [x23, w1, uxtw #2] /* a loaded base register overrides the writeback */
2:
    cbz w2, 4f
    NEXT_BIT
    BLK_LOAD w0
    str w0, [x23, w15, uxtw #2]
    b 2b
4:
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    b 9f
3: /* a stored base is the original value only when it is the lowest register listed */
    mov w17, #1
    lsl w17, w17, w1
    sub w17, w17, #1
    tst w2, w17
    csel w7, w4, w6, eq
6:
    cbz w2, 7f
    NEXT_BIT
    ldr w0, [x23, w15, uxtw #2]
    cmp w15, w1
    csel w0, w7, w0, eq
    BLK_STORE w0
    b 6b
7:
    str w6, [x23, w1, uxtw #2]
9:
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH

/* ARM LDM / STM with RAM, without the S bit, an empty list or PC as the base */
    .global a64_arm_block
    .type a64_arm_block, %function
a64_arm_block:
    tst w10, #(1 << 22)
    b.ne a64_arm_slow
    and w2, w10, #0xffff
    cbz w2, a64_arm_slow
    ubfx w1, w10, #16, #4
    cmp w1, #15
    b.eq a64_arm_slow
    POPCOUNT16
    ldr w4, [x23, w1, uxtw #2] /* base */
    add w6, w4, w3, lsl #2
    sub w7, w4, w3, lsl #2
    tst w10, #(1 << 23)
    b.eq 1f
    mov w5, w4 /* up: base (+ 4 if pre) */
    tst w10, #(1 << 24)
    b.eq 2f
    add w5, w5, #4
    b 2f
1:
    mov w5, w7 /* down: base - bytes (+ 4 if post) */
    mov w6, w7
    tst w10, #(1 << 24)
    b.ne 2f
    add w5, w5, #4
2: /* w6 = the new base */
    BLOCK_CHECK a64_arm_slow
    BLOCK_TIME a64_arm_slow
    ARM_FETCHED_W9
    mov w11, #0
    tst w10, #(1 << 20)
    b.eq 5f
    /* LDM */
    tst w10, #(1 << 21)
    b.eq 3f
    str w6, [x23, w1, uxtw #2] /* a loaded base register overrides the writeback */
3:
    and w0, w2, #0x8000
    and w2, w2, #0x7fff
4:
    cbz w2, 6f
    NEXT_BIT
    BLK_LOAD w4
    str w4, [x23, w15, uxtw #2]
    b 4b
6:
    cbz w0, 7f
    BLK_LOAD w4 /* PC */
    add x21, x21, #1 /* bus_idle(1) */
    and w20, w4, #~3
    REFILL 4, 1
    mov w26, #1
    ARM_DISPATCH
7:
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH
5: /* STM: a stored base is the original value only when it is the lowest register listed, or without writeback */
    mov w17, #1
    lsl w17, w17, w1
    sub w17, w17, #1
    tst w2, w17 /* Z: the base is the lowest listed */
    csel w7, w4, w6, eq
    tst w10, #(1 << 21)
    csel w7, w4, w7, eq
8:
    cbz w2, 9f
    NEXT_BIT
    ldr w0, [x23, w15, uxtw #2]
    cmp w15, w1
    csel w0, w7, w0, eq
    cmp w15, #15
    add w17, w20, #12 /* STM PC stores the address + 12 */
    csel w0, w17, w0, eq
    BLK_STORE w0
    b 8b
9:
    tst w10, #(1 << 21)
    b.eq 10f
    str w6, [x23, w1, uxtw #2]
10:
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH

/* ==== multiplies, halfword transfers, BX ==== */

/* w7 = the multiplier's array cycles for w3 (alu_mul_cycles with all-ones too): 1-4. */
.macro MUL_CYCLES
    eor w6, w3, w3, asr #31
    mov w7, #1
    cmp w6, #0x100
    cinc w7, w7, hs
    cmp w6, #0x10, lsl #12
    cinc w7, w7, hs
    lsr w8, w6, #24
    cmp w8, #0
    cinc w7, w7, ne
.endm

/* w4 = mul_carry(MUL_SHORT, w2 = multiplicand, w3 = multiplier, w5 = acc); keeps w0, w1, w11. */
.macro CALL_MUL_CARRY
    stp x0, x1, [sp, #-32]!
    str x11, [sp, #16]
    mov w0, #0
    mov w1, w2
    mov w2, w3
    mov w3, w5
    bl mul_carry
    mov w4, w0
    ldr x11, [sp, #16]
    ldp x0, x1, [sp], #32
.endm

/* ARM MUL / MLA: Rd = Rm * Rs (+ Rn); not PC as Rd. Like a data access: the next fetch is non-sequential. */
    .global a64_arm_mul
    .type a64_arm_mul, %function
a64_arm_mul:
    ubfx w1, w10, #16, #4
    cmp w1, #15
    b.eq a64_arm_slow
    ARM_FETCHED
    and w5, w10, #0xf
    ldr w2, [x23, w5, uxtw #2] /* m */
    ubfx w5, w10, #8, #4
    ldr w3, [x23, w5, uxtw #2] /* s */
    mov w5, #0
    tst w10, #(1 << 21)
    b.eq 1f
    ubfx w5, w10, #12, #4
    ldr w5, [x23, w5, uxtw #2] /* acc: read before Rd is written */
1:
    madd w0, w2, w3, w5
    MUL_CYCLES
    tst w10, #(1 << 21)
    cinc w7, w7, ne
    add x21, x21, x7 /* bus_idle(cycles) */
    mov w11, w7
    str w0, [x23, w1, uxtw #2]
    tst w10, #(1 << 20)
    b.eq 2f
    CALL_MUL_CARRY
    FLAGS_LOGIC /* N, Z; C from the Booth array; V kept */
2:
    add w20, w20, #4
    mov w12, #0 /* the bus was left free: a "RAM" access for the prefetch buffer */
    AFTER_DATA ARM_DISPATCH

/* THUMB 4 MUL: Rd = Rs * Rd (Rd is the multiplier); C from the Booth array, V kept */
a64_thumb_mul:
    THUMB_FETCHED
    and w1, w10, #7
    ldr w3, [x23, w1, uxtw #2] /* a: the multiplier */
    ubfx w5, w10, #3, #3
    ldr w2, [x23, w5, uxtw #2] /* b */
    mul w0, w2, w3
    MUL_CYCLES
    add x21, x21, x7
    mov w11, w7
    str w0, [x23, w1, uxtw #2]
    mov w5, #0
    CALL_MUL_CARRY
    FLAGS_LOGIC
    add w20, w20, #2
    mov w12, #0
    AFTER_DATA THUMB_DISPATCH

/* The rest of an ARM halfword / signed transfer: w5 the address, w4 the moved base, w1 Rd. Kinds as TMEM. */
.macro ARM_HALF_TAIL kind, size
.if \size == 2
    and w6, w5, #~1
.else
    mov w6, w5
.endif
    DATA_PTR (\kind == 2), \size, a64_arm_slow
    add x13, x24, w12, uxtw #2
    ldrb w13, [x13, #BUS_CYCLES]
    FETCH_CYCLES
    add x14, x21, x9
    cmp x14, x22
    b.hs a64_arm_slow
    ARM_FETCHED_W9
    add x21, x21, x13
    cmp w12, #8
    csel w11, w13, wzr, lo
.if \kind == 2
    ldr w0, [x23, w1, uxtw #2]
    strh w0, [x8]
    WRITEBACK
.else
  .if \kind == 5
    ldrh w0, [x8]
    and w9, w5, #1
    lsl w9, w9, #3
    ror w0, w0, w9
  .elseif \kind == 6
    ldrsb w0, [x8]
  .else
    ldrsh w0, [x8]
  .endif
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    WRITEBACK
    str w0, [x23, w1, uxtw #2]
.endif
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH
.endm

/* LDRH / STRH / LDRSB / LDRSH; not PC as Rd, not the ARMv5 doubleword forms. */
    .global a64_arm_half
    .type a64_arm_half, %function
a64_arm_half:
    ubfx w1, w10, #12, #4
    cmp w1, #15
    b.eq a64_arm_slow
    ubfx w7, w10, #5, #2 /* sh */
    tst w10, #(1 << 20)
    b.ne 1f
    cmp w7, #1
    b.ne a64_arm_slow /* L = 0 with sh 2 / 3: no effect on the ARM7TDMI */
1:
    add w9, w20, #8
    str w9, [x23, #R_PC] /* the base or the offset register may be PC */
    tst w10, #(1 << 22)
    b.eq 2f
    lsr w3, w10, #4 /* (op >> 4) & 0xF0 | (op & 0xF) */
    and w3, w3, #0xf0
    and w9, w10, #0xf
    orr w3, w3, w9
    b 3f
2:
    and w9, w10, #0xf
    ldr w3, [x23, w9, uxtw #2]
3:
    ubfx w2, w10, #16, #4
    ldr w2, [x23, w2, uxtw #2]
    add w4, w2, w3
    sub w5, w2, w3
    tst w10, #(1 << 23)
    csel w4, w4, w5, ne
    tst w10, #(1 << 24)
    csel w5, w4, w2, ne
    tst w10, #(1 << 20)
    b.ne 4f
    ARM_HALF_TAIL 2, 2
4:
    cmp w7, #2
    b.eq 5f
    b.hi 6f
    ARM_HALF_TAIL 5, 2
5:
    ARM_HALF_TAIL 6, 1
6:
    tst w5, #1
    b.ne 5b /* a misaligned LDRSH loads a signed byte */
    ARM_HALF_TAIL 7, 2

/* BX Rm: on in ARM state, or out to the Thumb loop. */
    .global a64_arm_bx
    .type a64_arm_bx, %function
a64_arm_bx:
    ARM_FETCHED
    and w5, w10, #0xf
    ldr w2, [x23, w5, uxtw #2]
    tbnz w2, #0, 1f
    and w20, w2, #~3
    REFILL 4, 1
    mov w26, #1
    ARM_DISPATCH
1:
    ldr w7, [x23, #R_CPSR]
    orr w7, w7, #0x20
    str w7, [x23, #R_CPSR]
    and w20, w2, #~1
    REFILL 2, 0
    mov w26, #1
    b .Larm_switch

/* THUMB 5 BX Rs: on in Thumb state, or out to the ARM loop. */
a64_thumb_bx:
    THUMB_FETCHED
    ubfx w5, w10, #3, #4
    ldr w2, [x23, w5, uxtw #2]
    tbz w2, #0, 1f
    and w20, w2, #~1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH
1:
    ldr w7, [x23, #R_CPSR]
    and w7, w7, #~0x20
    str w7, [x23, #R_CPSR]
    and w20, w2, #~3
    REFILL 4, 1
    mov w26, #1
    b .Lthumb_switch

    .section .data.rel.ro, "aw"
    .p2align 3
    .global a64_arm_single_handlers
.macro SGL_ENTRY hi
    .quad a64_arm_sgl_\hi
.endm
a64_arm_single_handlers: /* [bits 25-20] */
.irp hi, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
    SGL_ENTRY \hi
.endr
    .text

/* The handlers by class and variant, for cpu_a64.c's table (the order of the THUMB_CLASS_* enum is not used). */
    .section .data.rel.ro, "aw"
    .p2align 3
    .global a64_thumb_handlers
a64_thumb_handlers:
    .quad a64_thumb_shift_0, a64_thumb_shift_1, a64_thumb_shift_2, a64_thumb_slow                /* 0: shift by imm */
    .quad a64_thumb_addsub_0, a64_thumb_addsub_1, a64_thumb_addsub_2, a64_thumb_addsub_3         /* 4: add/sub */
    .quad a64_thumb_imm_0, a64_thumb_imm_1, a64_thumb_imm_2, a64_thumb_imm_3                     /* 8: imm8 */
    .quad a64_thumb_alu_0, a64_thumb_alu_1, a64_thumb_alu_2, a64_thumb_alu_3                     /* 12: ALU */
    .quad a64_thumb_alu_4, a64_thumb_alu_5, a64_thumb_alu_6, a64_thumb_alu_7
    .quad a64_thumb_alu_8, a64_thumb_alu_9, a64_thumb_alu_10, a64_thumb_alu_11
    .quad a64_thumb_alu_12, a64_thumb_mul, a64_thumb_alu_14, a64_thumb_alu_15
    .quad a64_thumb_hi_0, a64_thumb_hi_1, a64_thumb_hi_2, a64_thumb_bx                           /* 28: hi registers */
    .quad a64_thumb_regoff_0, a64_thumb_regoff_1, a64_thumb_regoff_2, a64_thumb_regoff_3         /* 32: [Rb, Ro] */
    .quad a64_thumb_regoffs_0, a64_thumb_regoffs_1, a64_thumb_regoffs_2, a64_thumb_regoffs_3     /* 36: signed */
    .quad a64_thumb_immoff_0, a64_thumb_immoff_1, a64_thumb_immoff_2, a64_thumb_immoff_3         /* 40: [Rb, #imm] */
    .quad a64_thumb_half_0, a64_thumb_half_1, a64_thumb_sprel_0, a64_thumb_sprel_1               /* 44 */
    .quad a64_thumb_ldaddr_0, a64_thumb_ldaddr_1, a64_thumb_pcload, a64_thumb_spadj              /* 48 */
    .quad a64_thumb_bcond, a64_thumb_b, a64_thumb_blhi, a64_thumb_bllo                           /* 52 */
    .quad a64_thumb_pushpop, a64_thumb_block                                                     /* 56 */
    .text

/* ---- the table of data-processing handlers, [opcode][S][kind] ---- */
    .section .data.rel.ro, "aw"
    .p2align 3
    .global a64_arm_dp_handlers
.macro DP_ENTRY opc, s, kind
    .quad a64_arm_dp_\opc\()_\s\()_\kind
.endm
a64_arm_dp_handlers:
.irp opc, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
  .irp s, 0,1
    .irp kind, 0,1,2,3,4
      .if ((\opc < 8) || (\opc > 11)) || \s
        DP_ENTRY \opc, \s, \kind
      .else
        .quad a64_arm_slow
      .endif
    .endr
  .endr
.endr

    .section .note.GNU-stack, "", %progbits
