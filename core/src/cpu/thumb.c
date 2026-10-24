#include "cpu/alu.h"
#include "cpu/cpu.h"
#include "cpu/cpu_mem.h"
#include "cpu/mul_carry.h"
#include "gba_internal.h"
#include "cpu/cpu_fast.h"

typedef void (*thumb_handler_t)(gba_t *g, uint16_t op);

static thumb_handler_t thumb_table[1024];

static void set_nzcv(arm_cpu_t *c, alu_result_t r) {
    cpu_set_nz(c, r.value);
    cpu_set_c(c, r.carry);
    cpu_set_v(c, r.overflow);
}

static void thumb_undefined(gba_t *g, uint16_t op) {
    (void)op;
    NOTE(g, NOTE_UNDEFINED, g->cpu.r[15] - 4);
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 2);
}

/* THUMB 1: LSL/LSR/ASR Rd, Rs, #imm5 */
static inline __attribute__((always_inline)) void thumb_shift_imm_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    bool carry = cpu_carry(c);
    uint32_t result = shift_by_imm((op >> 11) & 3u, c->r[(op >> 3) & 7], (op >> 6) & 0x1Fu, &carry);
    c->r[op & 7] = result;
    cpu_set_nz(c, result);
    cpu_set_c(c, carry);
}

/* THUMB 2: ADD/SUB Rd, Rs, Rn|#imm3 */
static inline __attribute__((always_inline)) void thumb_add_sub_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t a = c->r[(op >> 3) & 7];
    uint32_t b = (op & (1u << 10)) ? (op >> 6) & 7u : c->r[(op >> 6) & 7];
    alu_result_t r = (op & (1u << 9)) ? alu_add(a, ~b, 1) : alu_add(a, b, 0);
    c->r[op & 7] = r.value;
    set_nzcv(c, r);
}

/* THUMB 3: MOV/CMP/ADD/SUB Rd, #imm8 */
static inline __attribute__((always_inline)) void thumb_imm_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 8) & 7, imm = op & 0xFFu;
    alu_result_t r;
    switch ((op >> 11) & 3) {
    case 0:
        c->r[rd] = imm;
        cpu_set_nz(c, imm);
        return;
    case 1: set_nzcv(c, alu_add(c->r[rd], ~imm, 1)); return;
    case 2: r = alu_add(c->r[rd], imm, 0); break;
    default: r = alu_add(c->r[rd], ~imm, 1); break;
    }
    c->r[rd] = r.value;
    set_nzcv(c, r);
}

/* THUMB 4: ALU operations on low registers */
static inline __attribute__((always_inline)) void thumb_alu_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t a = c->r[rd], b = c->r[(op >> 3) & 7];
    bool carry = cpu_carry(c);
    uint32_t result;
    alu_result_t r;
    switch ((op >> 6) & 0xF) {
    case 0x0: result = a & b; break; /* AND */
    case 0x1: result = a ^ b; break; /* EOR */
    case 0x2: result = shift_by_reg(0, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* LSL */
    case 0x3: result = shift_by_reg(1, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* LSR */
    case 0x4: result = shift_by_reg(2, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* ASR */
    case 0x5: r = alu_add(a, b, cpu_carry(c)); c->r[rd] = r.value; set_nzcv(c, r); return;  /* ADC */
    case 0x6: r = alu_add(a, ~b, cpu_carry(c)); c->r[rd] = r.value; set_nzcv(c, r); return; /* SBC */
    case 0x7: result = shift_by_reg(3, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* ROR */
    case 0x8: cpu_set_nz(c, a & b); return;                                        /* TST */
    case 0x9: r = alu_add(0, ~b, 1); c->r[rd] = r.value; set_nzcv(c, r); return;   /* NEG */
    case 0xA: set_nzcv(c, alu_add(a, ~b, 1)); return;                              /* CMP */
    case 0xB: set_nzcv(c, alu_add(a, b, 0)); return;                               /* CMN */
    case 0xC: result = a | b; break;                                               /* ORR */
    case 0xD: /* MUL: Rd = Rs * Rd (Rd is the multiplier); C from the Booth array, V kept */
        result = a * b;
        carry = mul_carry(MUL_SHORT, b, a, 0);
        bus_idle(g, alu_mul_cycles(a, true));
        g->bus.data_access = true;
        g->bus.prefetch_hint = true;
        break;
    case 0xE: result = a & ~b; break;                                              /* BIC */
    default: result = ~b; break;                                                   /* MVN */
    }
    c->r[rd] = result;
    cpu_set_nz(c, result);
    cpu_set_c(c, carry); /* unchanged unless a shift produced a carry-out */
}

/* THUMB 5: ADD/CMP/MOV on high registers, and BX */
static inline __attribute__((always_inline)) void thumb_hi_reg_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op & 7u) | ((op >> 4) & 8u); /* H1 is bit 7 */
    uint32_t value = c->r[(op >> 3) & 0xF];     /* H2 + Rs are bits 6-3 */
    switch ((op >> 8) & 3) {
    case 0: {
        uint32_t result = c->r[rd] + value;
        if (rd == 15) cpu_branch(c, result & ~1u);
        else c->r[rd] = result;
        break;
    }
    case 1: set_nzcv(c, alu_add(c->r[rd], ~value, 1)); break;
    case 2:
        if (rd == 15) cpu_branch(c, value & ~1u);
        else c->r[rd] = value;
        break;
    default:
        if (value & 1) {
            cpu_branch(c, value & ~1u);
        } else {
            c->cpsr &= ~CPSR_T;
            cpu_branch(c, value & ~3u);
        }
        break;
    }
}

/* THUMB 6: LDR Rd, [PC, #imm8*4] — PC is word-aligned first */
static void thumb_pc_load(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t addr = (c->r[15] & ~2u) + (op & 0xFFu) * 4;
    c->r[(op >> 8) & 7] = bus_read32(g, addr, false);
    bus_idle(g, 1);
}

/* THUMB 7: STR/STRB/LDR/LDRB Rd, [Rb, Ro] */
static inline __attribute__((always_inline)) void thumb_reg_offset_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
    switch ((op >> 10) & 3) { /* bit 11 = L, bit 10 = B */
    case 0: bus_write32(g, addr, c->r[rd], false); break;
    case 1: bus_write8(g, addr, (uint8_t)c->r[rd], false); break;
    case 2: c->r[rd] = cpu_load_word(g, addr); bus_idle(g, 1); break;
    default: c->r[rd] = bus_read8(g, addr, false); bus_idle(g, 1); break;
    }
}

/* THUMB 8: STRH/LDSB/LDRH/LDSH Rd, [Rb, Ro] */
static inline __attribute__((always_inline)) void thumb_reg_offset_signed_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
    switch ((op >> 10) & 3) { /* bit 11 = H, bit 10 = S */
    case 0: bus_write16(g, addr, (uint16_t)c->r[rd], false); break;
    case 1: c->r[rd] = cpu_load_signed_byte(g, addr); bus_idle(g, 1); break;
    case 2: c->r[rd] = cpu_load_half(g, addr); bus_idle(g, 1); break;
    default: c->r[rd] = cpu_load_signed_half(g, addr); bus_idle(g, 1); break;
    }
}

/* THUMB 9: STR/LDR/STRB/LDRB Rd, [Rb, #imm5] */
static inline __attribute__((always_inline)) void thumb_imm_offset_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7, base = c->r[(op >> 3) & 7], imm = (op >> 6) & 0x1Fu;
    switch ((op >> 11) & 3) { /* bit 12 = B, bit 11 = L */
    case 0: bus_write32(g, base + imm * 4, c->r[rd], false); break;
    case 1: c->r[rd] = cpu_load_word(g, base + imm * 4); bus_idle(g, 1); break;
    case 2: bus_write8(g, base + imm, (uint8_t)c->r[rd], false); break;
    default: c->r[rd] = bus_read8(g, base + imm, false); bus_idle(g, 1); break;
    }
}

/* THUMB 10: STRH/LDRH Rd, [Rb, #imm5*2] */
static inline __attribute__((always_inline)) void thumb_half_imm_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + ((op >> 6) & 0x1Fu) * 2;
    if (op & (1u << 11)) {
        c->r[rd] = cpu_load_half(g, addr);
        bus_idle(g, 1);
    } else {
        bus_write16(g, addr, (uint16_t)c->r[rd], false);
    }
}

/* THUMB 11: STR/LDR Rd, [SP, #imm8*4] */
static inline __attribute__((always_inline)) void thumb_sp_relative_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 8) & 7;
    uint32_t addr = c->r[13] + (op & 0xFFu) * 4;
    if (op & (1u << 11)) {
        c->r[rd] = cpu_load_word(g, addr);
        bus_idle(g, 1);
    } else {
        bus_write32(g, addr, c->r[rd], false);
    }
}

/* THUMB 12: ADD Rd, PC|SP, #imm8*4 */
static inline __attribute__((always_inline)) void thumb_load_address_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t base = (op & (1u << 11)) ? c->r[13] : (c->r[15] & ~2u);
    c->r[(op >> 8) & 7] = base + (op & 0xFFu) * 4;
}

/* THUMB 13: ADD SP, #±imm7*4 */
static void thumb_sp_adjust(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t offset = (op & 0x7Fu) * 4;
    c->r[13] = (op & 0x80) ? c->r[13] - offset : c->r[13] + offset;
}

/* THUMB 14: PUSH {rlist, LR} / POP {rlist, PC}. SP keeps any misalignment; accesses are aligned. */
static inline __attribute__((always_inline)) void thumb_push_pop_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rlist = op & 0xFFu;
    bool extra = (op & (1u << 8)) != 0; /* LR for PUSH, PC for POP */
    uint32_t count = (uint32_t)__builtin_popcount(rlist) + (extra ? 1 : 0);
    bool seq = false;

    if (op & (1u << 11)) { /* POP = LDMIA sp! */
        uint32_t addr = c->r[13];
        if (count == 0) { /* ARMv4 empty-list quirk: loads PC, SP moves by 0x40 */
            uint32_t pc = bus_read32(g, addr, false);
            c->r[13] = addr + 0x40;
            cpu_branch(c, pc & ~1u);
            return;
        }
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (extra) {
            uint32_t pc = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            addr += 4;
            cpu_branch(c, pc & ~1u); /* ARMv4: POP {pc} does not change state */
        }
        c->r[13] = addr;
        bus_idle(g, 1);
    } else { /* PUSH = STMDB sp! */
        if (count == 0) {
            c->r[13] -= 0x40;
            bus_write32(g, c->r[13], c->r[15] + 2, false);
            return;
        }
        uint32_t addr = c->r[13] - count * 4;
        c->r[13] = addr;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            bus_write32(g, addr, c->r[i], seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (extra) bus_write32(g, addr, c->r[14], seq && (addr & 0x00FFFFFF) != 0);
    }
}

/* THUMB 15: STMIA/LDMIA Rb!, {rlist} */
static inline __attribute__((always_inline)) void thumb_block_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rb = (op >> 8) & 7, rlist = op & 0xFFu;
    uint32_t base = c->r[rb], addr = base;
    bool load = (op & (1u << 11)) != 0;
    bool seq = false;

    if (rlist == 0) { /* ARMv4 quirk: transfers PC, base moves by 0x40 */
        if (load) {
            uint32_t pc = bus_read32(g, addr, false);
            c->r[rb] = base + 0x40;
            cpu_branch(c, pc & ~1u);
        } else {
            bus_write32(g, addr, c->r[15] + 2, false); /* instruction address + 6 */
            c->r[rb] = base + 0x40;
        }
        return;
    }

    uint32_t new_base = base + (uint32_t)__builtin_popcount(rlist) * 4;
    if (load) {
        c->r[rb] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        bus_idle(g, 1);
    } else {
        bool base_first = (rlist & ((1u << rb) - 1)) == 0;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value = (i == rb) ? (base_first ? base : new_base) : c->r[i];
            bus_write32(g, addr, value, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        c->r[rb] = new_base;
    }
}

/* THUMB 16: B<cond> label */
static inline __attribute__((always_inline)) void thumb_cond_branch_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    if (!cpu_cond_passed(c->cpsr, (op >> 8) & 0xFu)) return;
    int32_t offset = (int8_t)(op & 0xFF) * 2;
    cpu_branch(c, c->r[15] + (uint32_t)offset);
}

/* THUMB 17: SWI */
static void thumb_swi(gba_t *g, uint16_t op) {
    (void)op;
    cpu_exception(g, 0x08, MODE_SVC, g->cpu.r[15] - 2);
}

/* THUMB 18: B label */
static void thumb_branch(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    int32_t offset = ((int32_t)((uint32_t)op << 21)) >> 20; /* sign-extended offset11 * 2 */
    cpu_branch(c, c->r[15] + (uint32_t)offset);
}

/* THUMB 19, first half: LR = PC + (offset11 << 12) */
static void thumb_bl_high(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    int32_t offset = ((int32_t)((uint32_t)op << 21)) >> 9;
    c->r[14] = c->r[15] + (uint32_t)offset;
}

/* THUMB 19, second half: branch to LR + (offset11 << 1); LR = next instruction | 1 */
static void thumb_bl_low(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t target = c->r[14] + (op & 0x7FFu) * 2;
    c->r[14] = (c->r[15] - 2) | 1;
    cpu_branch(c, target & ~1u);
}


/* Specialised copies: the variant bits (a constant in each copy) fold the handlers' switches away. */
#define THUMB_COPY(name, mask, value) \
    static void name##_##value(gba_t *g, uint16_t op) { name##_body(g, (uint16_t)((op & ~(mask)) | (value))); }
THUMB_COPY(thumb_shift_imm, 0x1800, 0x0)
THUMB_COPY(thumb_shift_imm, 0x1800, 0x800)
THUMB_COPY(thumb_shift_imm, 0x1800, 0x1000)
THUMB_COPY(thumb_add_sub, 0x0600, 0x0)
THUMB_COPY(thumb_add_sub, 0x0600, 0x200)
THUMB_COPY(thumb_add_sub, 0x0600, 0x400)
THUMB_COPY(thumb_add_sub, 0x0600, 0x600)
THUMB_COPY(thumb_imm, 0x1800, 0x0)
THUMB_COPY(thumb_imm, 0x1800, 0x800)
THUMB_COPY(thumb_imm, 0x1800, 0x1000)
THUMB_COPY(thumb_imm, 0x1800, 0x1800)
THUMB_COPY(thumb_alu, 0x03C0, 0x0)
THUMB_COPY(thumb_alu, 0x03C0, 0x40)
THUMB_COPY(thumb_alu, 0x03C0, 0x80)
THUMB_COPY(thumb_alu, 0x03C0, 0xC0)
THUMB_COPY(thumb_alu, 0x03C0, 0x100)
THUMB_COPY(thumb_alu, 0x03C0, 0x140)
THUMB_COPY(thumb_alu, 0x03C0, 0x180)
THUMB_COPY(thumb_alu, 0x03C0, 0x1C0)
THUMB_COPY(thumb_alu, 0x03C0, 0x200)
THUMB_COPY(thumb_alu, 0x03C0, 0x240)
THUMB_COPY(thumb_alu, 0x03C0, 0x280)
THUMB_COPY(thumb_alu, 0x03C0, 0x2C0)
THUMB_COPY(thumb_alu, 0x03C0, 0x300)
THUMB_COPY(thumb_alu, 0x03C0, 0x340)
THUMB_COPY(thumb_alu, 0x03C0, 0x380)
THUMB_COPY(thumb_alu, 0x03C0, 0x3C0)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x0)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x100)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x200)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x300)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0x0)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0x400)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0x800)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0xC00)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0x0)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0x400)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0x800)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0xC00)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x0)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x800)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x1000)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x1800)
THUMB_COPY(thumb_half_imm, 0x0800, 0x0)
THUMB_COPY(thumb_half_imm, 0x0800, 0x800)
THUMB_COPY(thumb_sp_relative, 0x0800, 0x0)
THUMB_COPY(thumb_sp_relative, 0x0800, 0x800)
THUMB_COPY(thumb_load_address, 0x0800, 0x0)
THUMB_COPY(thumb_load_address, 0x0800, 0x800)
THUMB_COPY(thumb_push_pop, 0x0900, 0x0)
THUMB_COPY(thumb_push_pop, 0x0900, 0x100)
THUMB_COPY(thumb_push_pop, 0x0900, 0x800)
THUMB_COPY(thumb_push_pop, 0x0900, 0x900)
THUMB_COPY(thumb_block, 0x0800, 0x0)
THUMB_COPY(thumb_block, 0x0800, 0x800)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x0)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x100)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x200)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x300)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x400)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x500)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x600)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x700)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x800)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x900)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xA00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xB00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xC00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xD00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xE00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xF00)
static const thumb_handler_t thumb_shift_imm_copies[] = {thumb_shift_imm_0x0, thumb_shift_imm_0x800, thumb_shift_imm_0x1000};
static const thumb_handler_t thumb_add_sub_copies[] = {thumb_add_sub_0x0, thumb_add_sub_0x200, thumb_add_sub_0x400, thumb_add_sub_0x600};
static const thumb_handler_t thumb_imm_copies[] = {thumb_imm_0x0, thumb_imm_0x800, thumb_imm_0x1000, thumb_imm_0x1800};
static const thumb_handler_t thumb_alu_copies[] = {thumb_alu_0x0, thumb_alu_0x40, thumb_alu_0x80, thumb_alu_0xC0, thumb_alu_0x100, thumb_alu_0x140, thumb_alu_0x180, thumb_alu_0x1C0, thumb_alu_0x200, thumb_alu_0x240, thumb_alu_0x280, thumb_alu_0x2C0, thumb_alu_0x300, thumb_alu_0x340, thumb_alu_0x380, thumb_alu_0x3C0};
static const thumb_handler_t thumb_hi_reg_copies[] = {thumb_hi_reg_0x0, thumb_hi_reg_0x100, thumb_hi_reg_0x200, thumb_hi_reg_0x300};
static const thumb_handler_t thumb_reg_offset_copies[] = {thumb_reg_offset_0x0, thumb_reg_offset_0x400, thumb_reg_offset_0x800, thumb_reg_offset_0xC00};
static const thumb_handler_t thumb_reg_offset_signed_copies[] = {thumb_reg_offset_signed_0x0, thumb_reg_offset_signed_0x400, thumb_reg_offset_signed_0x800, thumb_reg_offset_signed_0xC00};
static const thumb_handler_t thumb_imm_offset_copies[] = {thumb_imm_offset_0x0, thumb_imm_offset_0x800, thumb_imm_offset_0x1000, thumb_imm_offset_0x1800};
static const thumb_handler_t thumb_half_imm_copies[] = {thumb_half_imm_0x0, thumb_half_imm_0x800};
static const thumb_handler_t thumb_sp_relative_copies[] = {thumb_sp_relative_0x0, thumb_sp_relative_0x800};
static const thumb_handler_t thumb_load_address_copies[] = {thumb_load_address_0x0, thumb_load_address_0x800};
static const thumb_handler_t thumb_push_pop_copies[] = {thumb_push_pop_0x0, thumb_push_pop_0x100, thumb_push_pop_0x800, thumb_push_pop_0x900};
static const thumb_handler_t thumb_block_copies[] = {thumb_block_0x0, thumb_block_0x800};
static const thumb_handler_t thumb_cond_branch_copies[] = {thumb_cond_branch_0x0, thumb_cond_branch_0x100, thumb_cond_branch_0x200, thumb_cond_branch_0x300, thumb_cond_branch_0x400, thumb_cond_branch_0x500, thumb_cond_branch_0x600, thumb_cond_branch_0x700, thumb_cond_branch_0x800, thumb_cond_branch_0x900, thumb_cond_branch_0xA00, thumb_cond_branch_0xB00, thumb_cond_branch_0xC00, thumb_cond_branch_0xD00, thumb_cond_branch_0xE00, thumb_cond_branch_0xF00};

/* idx = instruction bits 15-6 */
static thumb_handler_t thumb_classify(uint32_t idx) {
    uint32_t op = idx << 6;
    if ((op & 0xF800) == 0x1800) return thumb_add_sub_copies[(op & 0x0600) >> 9];           /* 00011 */
    if ((op & 0xE000) == 0x0000) return thumb_shift_imm_copies[(op & 0x1800) >> 11];         /* 000xx */
    if ((op & 0xE000) == 0x2000) return thumb_imm_copies[(op & 0x1800) >> 11];               /* 001xx */
    if ((op & 0xFC00) == 0x4000) return thumb_alu_copies[(op & 0x03C0) >> 6];               /* 010000 */
    if ((op & 0xFC00) == 0x4400) return thumb_hi_reg_copies[(op & 0x0300) >> 8];            /* 010001 */
    if ((op & 0xF800) == 0x4800) return thumb_pc_load;           /* 01001 */
    if ((op & 0xF200) == 0x5000) return thumb_reg_offset_copies[(op & 0x0C00) >> 10];        /* 0101xx0 */
    if ((op & 0xF200) == 0x5200) return thumb_reg_offset_signed_copies[(op & 0x0C00) >> 10]; /* 0101xx1 */
    if ((op & 0xE000) == 0x6000) return thumb_imm_offset_copies[(op & 0x1800) >> 11];        /* 011xx */
    if ((op & 0xF000) == 0x8000) return thumb_half_imm_copies[(op & 0x0800) >> 11];          /* 1000x */
    if ((op & 0xF000) == 0x9000) return thumb_sp_relative_copies[(op & 0x0800) >> 11];       /* 1001x */
    if ((op & 0xF000) == 0xA000) return thumb_load_address_copies[(op & 0x0800) >> 11];      /* 1010x */
    if ((op & 0xFF00) == 0xB000) return thumb_sp_adjust;         /* 10110000 */
    if ((op & 0xF600) == 0xB400) return thumb_push_pop_copies[((op >> 10) & 2) | ((op >> 8) & 1)];          /* 1011x10x */
    if ((op & 0xF000) == 0xC000) return thumb_block_copies[(op & 0x0800) >> 11];             /* 1100x */
    if ((op & 0xFF00) == 0xDF00) return thumb_swi;               /* 11011111 */
    if ((op & 0xFF00) == 0xDE00) return thumb_undefined;         /* 11011110 */
    if ((op & 0xF000) == 0xD000) return thumb_cond_branch_copies[(op & 0x0F00) >> 8];       /* 1101 */
    if ((op & 0xF800) == 0xE000) return thumb_branch;            /* 11100 */
    if ((op & 0xF800) == 0xF000) return thumb_bl_high;           /* 11110 */
    if ((op & 0xF800) == 0xF800) return thumb_bl_low;            /* 11111 */
    return thumb_undefined; /* 11101 (BLX on ARMv5) and unused 1011 encodings */
}

static void thumb_fast_init_table(void);

int thumb_class(uint32_t i) {
    thumb_handler_t h = thumb_table[i];
    static const struct {
        const thumb_handler_t *copies;
        size_t n;
        int cls;
    } families[] = {
        {thumb_shift_imm_copies, 3, THUMB_CLASS_SHIFT_IMM}, {thumb_add_sub_copies, 4, THUMB_CLASS_ADD_SUB},
        {thumb_imm_copies, 4, THUMB_CLASS_IMM}, {thumb_alu_copies, 16, THUMB_CLASS_ALU},
        {thumb_hi_reg_copies, 4, THUMB_CLASS_HI_REG}, {thumb_reg_offset_copies, 4, THUMB_CLASS_REG_OFFSET},
        {thumb_reg_offset_signed_copies, 4, THUMB_CLASS_REG_OFFSET_SIGNED}, {thumb_imm_offset_copies, 4, THUMB_CLASS_IMM_OFFSET},
        {thumb_half_imm_copies, 2, THUMB_CLASS_HALF_IMM}, {thumb_sp_relative_copies, 2, THUMB_CLASS_SP_RELATIVE},
        {thumb_load_address_copies, 2, THUMB_CLASS_LOAD_ADDRESS}, {thumb_push_pop_copies, 4, THUMB_CLASS_PUSH_POP},
        {thumb_block_copies, 2, THUMB_CLASS_BLOCK}, {thumb_cond_branch_copies, 16, THUMB_CLASS_COND_BRANCH},
    };
    for (size_t f = 0; f < sizeof families / sizeof families[0]; f++)
        for (size_t k = 0; k < families[f].n; k++)
            if (h == families[f].copies[k]) return families[f].cls;
    if (h == thumb_pc_load) return THUMB_CLASS_PC_LOAD;
    if (h == thumb_sp_adjust) return THUMB_CLASS_SP_ADJUST;
    if (h == thumb_branch) return THUMB_CLASS_BRANCH;
    if (h == thumb_bl_high) return THUMB_CLASS_BL_HIGH;
    if (h == thumb_bl_low) return THUMB_CLASS_BL_LOW;
    return THUMB_CLASS_OTHER;
}

void thumb_init_table(void) {
    for (uint32_t i = 0; i < 1024; i++) thumb_table[i] = thumb_classify(i);
    thumb_fast_init_table();
}

void thumb_execute(gba_t *g, uint16_t op) {
    thumb_table[op >> 6](g, op);
}

/* ---- The threaded loop's Thumb handlers (cpu_fast.h) ---- */

thumb_fast_t *thumb_fast_table[1024];

void thumb_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    fast_generic_begin(g);
    thumb_fetched(g, pc, op, seq);
    thumb_execute(g, (uint16_t)op);
    if (!fast_generic_end(g, pc + 2) || !(g->cpu.cpsr & CPSR_T)) return; /* r15 and next_fetch_seq are stored */
    THUMB_NEXT(g, g->cpu.r[15], g->cpu.next_fetch_seq);
}

/* Instructions that touch neither the bus nor PC: the fetch, the handler, the next instruction. */
#define THUMB_PLAIN(copy)                                                              \
    static void copy##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {           \
        thumb_fetched(g, pc, op, seq);                                                 \
        copy(g, (uint16_t)op);                                                         \
        THUMB_NEXT(g, pc + 2, true);                                                   \
    }
THUMB_PLAIN(thumb_shift_imm_0x0) THUMB_PLAIN(thumb_shift_imm_0x800) THUMB_PLAIN(thumb_shift_imm_0x1000)
THUMB_PLAIN(thumb_add_sub_0x0) THUMB_PLAIN(thumb_add_sub_0x200) THUMB_PLAIN(thumb_add_sub_0x400) THUMB_PLAIN(thumb_add_sub_0x600)
THUMB_PLAIN(thumb_imm_0x0) THUMB_PLAIN(thumb_imm_0x800) THUMB_PLAIN(thumb_imm_0x1000) THUMB_PLAIN(thumb_imm_0x1800)
THUMB_PLAIN(thumb_alu_0x0) THUMB_PLAIN(thumb_alu_0x40) THUMB_PLAIN(thumb_alu_0x80) THUMB_PLAIN(thumb_alu_0xC0)
THUMB_PLAIN(thumb_alu_0x100) THUMB_PLAIN(thumb_alu_0x140) THUMB_PLAIN(thumb_alu_0x180) THUMB_PLAIN(thumb_alu_0x1C0)
THUMB_PLAIN(thumb_alu_0x200) THUMB_PLAIN(thumb_alu_0x240) THUMB_PLAIN(thumb_alu_0x280) THUMB_PLAIN(thumb_alu_0x2C0)
THUMB_PLAIN(thumb_alu_0x300) THUMB_PLAIN(thumb_alu_0x380) THUMB_PLAIN(thumb_alu_0x3C0) /* not 0x340, MUL */
THUMB_PLAIN(thumb_load_address_0x0) THUMB_PLAIN(thumb_load_address_0x800)
THUMB_PLAIN(thumb_sp_adjust) THUMB_PLAIN(thumb_bl_high)

/* ADD / CMP / MOV with high registers, not writing PC. */
#define THUMB_HI(copy)                                                                 \
    static void copy##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {           \
        if (((op >> 8) & 1) == 0 && ((op & 7u) | ((op >> 4) & 8u)) == 15)              \
            FAST_TAIL return thumb_fast_generic(g, pc, op, seq);                       \
        thumb_fetched(g, pc, op, seq);                                                 \
        copy(g, (uint16_t)op);                                                         \
        THUMB_NEXT(g, pc + 2, true);                                                   \
    }
THUMB_HI(thumb_hi_reg_0x0) THUMB_HI(thumb_hi_reg_0x100) THUMB_HI(thumb_hi_reg_0x200)

/* B<cond> */
#define THUMB_COND(cond)                                                                       \
    static void thumb_cond_##cond##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {      \
        thumb_fetched(g, pc, op, seq);                                                         \
        if (!cpu_cond_passed(g->cpu.cpsr, cond)) THUMB_NEXT(g, pc + 2, true);                  \
        uint32_t target = pc + 4 + (uint32_t)((int8_t)(op & 0xFF) * 2);                        \
        fast_refill(g, target, 2);                                                             \
        THUMB_NEXT(g, target, true);                                                           \
    }
THUMB_COND(0) THUMB_COND(1) THUMB_COND(2) THUMB_COND(3) THUMB_COND(4) THUMB_COND(5) THUMB_COND(6)
THUMB_COND(7) THUMB_COND(8) THUMB_COND(9) THUMB_COND(10) THUMB_COND(11) THUMB_COND(12) THUMB_COND(13)

/* B */
static void thumb_branch_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    thumb_fetched(g, pc, op, seq);
    uint32_t target = pc + 4 + (uint32_t)(((int32_t)(op << 21)) >> 20);
    fast_refill(g, target, 2);
    THUMB_NEXT(g, target, true);
}

/* BL, second half */
static void thumb_bl_low_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    thumb_fetched(g, pc, op, seq);
    uint32_t target = (g->cpu.r[14] + (op & 0x7FFu) * 2) & ~1u;
    g->cpu.r[14] = (pc + 2) | 1;
    fast_refill(g, target, 2);
    THUMB_NEXT(g, target, true);
}

enum { TM_STR, TM_STRB, TM_STRH, TM_LDR, TM_LDRB, TM_LDRH, TM_LDSB, TM_LDSH };

/* One load or store at addr with RAM (or a load from the ROM), as the handlers above do it with the bus. Returns
 * the next fetch's sequential flag, or -1 before any side effect when the access needs the bus. */
static inline __attribute__((always_inline)) int thumb_fast_mem(gba_t *g, uint32_t pc, uint32_t op, bool seq,
                                                                 uint32_t addr, uint32_t rd, int kind) {
    unsigned size = kind == TM_STR || kind == TM_LDR ? 4 : kind == TM_STRB || kind == TM_LDRB || kind == TM_LDSB ? 1 : 2;
    if (kind == TM_LDSH && (addr & 1)) size = 1; /* misaligned: a signed byte */
    uint32_t a = addr & ~(uint32_t)(size - 1);
    bool store = kind <= TM_STRH;
    uint8_t *p = fast_data_ptr(g, a, size, store);
    if (!p || !fast_data_ok(g, bus_access_cycles(g, pc & ~1u, 2, seq))) return -1; /* needs the bus */
    thumb_fetched(g, pc, op, seq);
    fast_data_begin(g);
    fast_data_cycles(g, a, size);
    uint32_t *r = g->cpu.r;
    switch (kind) {
    case TM_STR: bus_store32(p, r[rd]); break;
    case TM_STRB: p[0] = (uint8_t)r[rd]; break;
    case TM_STRH: bus_store16(p, r[rd]); break;
    case TM_LDR: r[rd] = ror32(bus_load_n(p, 4), (addr & 3) * 8); break;
    case TM_LDRB: r[rd] = p[0]; break;
    case TM_LDRH: r[rd] = ror32(bus_load_n(p, 2), (addr & 1) * 8); break;
    case TM_LDSB: r[rd] = (uint32_t)(int32_t)(int8_t)p[0]; break;
    default: r[rd] = size == 1 ? (uint32_t)(int32_t)(int8_t)p[0] : (uint32_t)(int32_t)(int16_t)bus_load_n(p, 2); break;
    }
    if (!store) bus_idle(g, 1);
    return fast_after_data(g, pc + 2);
}

/* THUMB 6: LDR Rd, [PC, #imm8*4] */
static void thumb_pc_load_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    int next_seq = thumb_fast_mem(g, pc, op, seq, ((pc + 4) & ~2u) + (op & 0xFFu) * 4, (op >> 8) & 7, TM_LDR);
    if (next_seq < 0) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);
    THUMB_NEXT(g, pc + 2, next_seq);
}

#define THUMB_MEM(name, kind, addr_expr, rd_expr)                                                   \
    static void name##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {                         \
        uint32_t *r = g->cpu.r;                                                                     \
        int next_seq = thumb_fast_mem(g, pc, op, seq, (addr_expr), (rd_expr), kind);                \
        if (next_seq < 0) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);                      \
        THUMB_NEXT(g, pc + 2, next_seq);                                                            \
    }
/* THUMB 7 / 8: [Rb, Ro] */
#define RB_RO (r[(op >> 3) & 7] + r[(op >> 6) & 7])
THUMB_MEM(thumb_reg_offset_0x0, TM_STR, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_0x400, TM_STRB, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_0x800, TM_LDR, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_0xC00, TM_LDRB, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0x0, TM_STRH, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0x400, TM_LDSB, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0x800, TM_LDRH, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0xC00, TM_LDSH, RB_RO, op & 7)
/* THUMB 9 / 10: [Rb, #imm5] */
#define RB_IMM(scale) (r[(op >> 3) & 7] + ((op >> 6) & 0x1Fu) * (scale))
THUMB_MEM(thumb_imm_offset_0x0, TM_STR, RB_IMM(4), op & 7)
THUMB_MEM(thumb_imm_offset_0x800, TM_LDR, RB_IMM(4), op & 7)
THUMB_MEM(thumb_imm_offset_0x1000, TM_STRB, RB_IMM(1), op & 7)
THUMB_MEM(thumb_imm_offset_0x1800, TM_LDRB, RB_IMM(1), op & 7)
THUMB_MEM(thumb_half_imm_0x0, TM_STRH, RB_IMM(2), op & 7)
THUMB_MEM(thumb_half_imm_0x800, TM_LDRH, RB_IMM(2), op & 7)
/* THUMB 11: [SP, #imm8*4] */
THUMB_MEM(thumb_sp_relative_0x0, TM_STR, r[13] + (op & 0xFFu) * 4, (op >> 8) & 7)
THUMB_MEM(thumb_sp_relative_0x800, TM_LDR, r[13] + (op & 0xFFu) * 4, (op >> 8) & 7)

/* PUSH / POP with RAM, not an empty list. */
static void thumb_push_pop_fast_any(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rlist = op & 0xFFu;
    bool extra = (op & (1u << 8)) != 0, pop = (op & (1u << 11)) != 0;
    uint32_t count = (uint32_t)__builtin_popcount(rlist) + (extra ? 1 : 0);
    uint32_t addr = pop ? c->r[13] : c->r[13] - count * 4;
    uint32_t mask, cyc;
    uint8_t *mem = count ? fast_block_ptr(g, addr, count, bus_access_cycles(g, pc & ~1u, 2, seq), &mask, &cyc) : NULL;
    if (!mem) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);
    thumb_fetched(g, pc, op, seq);
    fast_data_begin(g);
    g->bus.prefetch_hint = true;
    if (pop) {
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = fast_block_load(g, mem, mask, cyc, addr);
            addr += 4;
        }
        uint32_t target = 0;
        if (extra) {
            target = fast_block_load(g, mem, mask, cyc, addr) & ~1u; /* ARMv4: POP {pc} does not change state */
            addr += 4;
        }
        c->r[13] = addr;
        bus_idle(g, 1);
        if (extra) {
            fast_refill(g, target, 2);
            THUMB_NEXT(g, target, true);
        }
    } else {
        c->r[13] = addr;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            fast_block_store(g, mem, mask, cyc, addr, c->r[i]);
            addr += 4;
        }
        if (extra) fast_block_store(g, mem, mask, cyc, addr, c->r[14]);
    }
    THUMB_NEXT(g, pc + 2, fast_after_data(g, pc + 2));
}

/* STMIA / LDMIA Rb!, {rlist} with RAM, not an empty list. */
static void thumb_block_fast_any(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rb = (op >> 8) & 7, rlist = op & 0xFFu;
    uint32_t base = c->r[rb], addr = base, count = (uint32_t)__builtin_popcount(rlist);
    uint32_t mask, cyc;
    uint8_t *mem = count ? fast_block_ptr(g, addr, count, bus_access_cycles(g, pc & ~1u, 2, seq), &mask, &cyc) : NULL;
    if (!mem) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);
    thumb_fetched(g, pc, op, seq);
    fast_data_begin(g);
    g->bus.prefetch_hint = true;
    uint32_t new_base = base + count * 4;
    if (op & (1u << 11)) {
        c->r[rb] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = fast_block_load(g, mem, mask, cyc, addr);
            addr += 4;
        }
        bus_idle(g, 1);
    } else {
        bool base_first = (rlist & ((1u << rb) - 1)) == 0;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            fast_block_store(g, mem, mask, cyc, addr, (i == rb) ? (base_first ? base : new_base) : c->r[i]);
            addr += 4;
        }
        c->r[rb] = new_base;
    }
    THUMB_NEXT(g, pc + 2, fast_after_data(g, pc + 2));
}

static void thumb_fast_enter(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    (void)op;
    THUMB_NEXT(g, pc, seq);
}

void thumb_fast_run(gba_t *g) {
    thumb_fast_enter(g, g->cpu.r[15], 0, g->cpu.next_fetch_seq);
}

static void thumb_fast_init_table(void) {
#define PAIR(copy) {copy, copy##_fast}
    static const struct {
        thumb_handler_t slow;
        thumb_fast_t *fast;
    } pairs[] = {
        PAIR(thumb_shift_imm_0x0), PAIR(thumb_shift_imm_0x800), PAIR(thumb_shift_imm_0x1000),
        PAIR(thumb_add_sub_0x0), PAIR(thumb_add_sub_0x200), PAIR(thumb_add_sub_0x400), PAIR(thumb_add_sub_0x600),
        PAIR(thumb_imm_0x0), PAIR(thumb_imm_0x800), PAIR(thumb_imm_0x1000), PAIR(thumb_imm_0x1800),
        PAIR(thumb_alu_0x0), PAIR(thumb_alu_0x40), PAIR(thumb_alu_0x80), PAIR(thumb_alu_0xC0), PAIR(thumb_alu_0x100),
        PAIR(thumb_alu_0x140), PAIR(thumb_alu_0x180), PAIR(thumb_alu_0x1C0), PAIR(thumb_alu_0x200), PAIR(thumb_alu_0x240),
        PAIR(thumb_alu_0x280), PAIR(thumb_alu_0x2C0), PAIR(thumb_alu_0x300), PAIR(thumb_alu_0x380), PAIR(thumb_alu_0x3C0),
        PAIR(thumb_load_address_0x0), PAIR(thumb_load_address_0x800), PAIR(thumb_sp_adjust), PAIR(thumb_bl_high),
        PAIR(thumb_hi_reg_0x0), PAIR(thumb_hi_reg_0x100), PAIR(thumb_hi_reg_0x200),
        {thumb_cond_branch_0x0, thumb_cond_0_fast}, {thumb_cond_branch_0x100, thumb_cond_1_fast},
        {thumb_cond_branch_0x200, thumb_cond_2_fast}, {thumb_cond_branch_0x300, thumb_cond_3_fast},
        {thumb_cond_branch_0x400, thumb_cond_4_fast}, {thumb_cond_branch_0x500, thumb_cond_5_fast},
        {thumb_cond_branch_0x600, thumb_cond_6_fast}, {thumb_cond_branch_0x700, thumb_cond_7_fast},
        {thumb_cond_branch_0x800, thumb_cond_8_fast}, {thumb_cond_branch_0x900, thumb_cond_9_fast},
        {thumb_cond_branch_0xA00, thumb_cond_10_fast}, {thumb_cond_branch_0xB00, thumb_cond_11_fast},
        {thumb_cond_branch_0xC00, thumb_cond_12_fast}, {thumb_cond_branch_0xD00, thumb_cond_13_fast},
        PAIR(thumb_branch), PAIR(thumb_bl_low), PAIR(thumb_pc_load),
        PAIR(thumb_reg_offset_0x0), PAIR(thumb_reg_offset_0x400), PAIR(thumb_reg_offset_0x800), PAIR(thumb_reg_offset_0xC00),
        PAIR(thumb_reg_offset_signed_0x0), PAIR(thumb_reg_offset_signed_0x400), PAIR(thumb_reg_offset_signed_0x800),
        PAIR(thumb_reg_offset_signed_0xC00), PAIR(thumb_imm_offset_0x0), PAIR(thumb_imm_offset_0x800),
        PAIR(thumb_imm_offset_0x1000), PAIR(thumb_imm_offset_0x1800), PAIR(thumb_half_imm_0x0), PAIR(thumb_half_imm_0x800),
        PAIR(thumb_sp_relative_0x0), PAIR(thumb_sp_relative_0x800),
        {thumb_push_pop_0x0, thumb_push_pop_fast_any}, {thumb_push_pop_0x100, thumb_push_pop_fast_any},
        {thumb_push_pop_0x800, thumb_push_pop_fast_any}, {thumb_push_pop_0x900, thumb_push_pop_fast_any},
        {thumb_block_0x0, thumb_block_fast_any}, {thumb_block_0x800, thumb_block_fast_any},
    };
#undef PAIR
    for (uint32_t i = 0; i < 1024; i++) {
        thumb_fast_table[i] = thumb_fast_generic;
        for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++)
            if (thumb_table[i] == pairs[k].slow) thumb_fast_table[i] = pairs[k].fast;
    }
}
