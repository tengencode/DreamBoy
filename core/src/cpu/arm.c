#include "bios/hle.h"
#include "cpu/alu.h"
#include "cpu/cpu.h"
#include "cpu/cpu_mem.h"
#include "cpu/mul_carry.h"
#include "gba_internal.h"
#include "cpu/cpu_fast.h"

typedef void (*arm_handler_t)(gba_t *g, uint32_t op);

static arm_handler_t arm_table[4096];

#define BIT(op, n) (((op) >> (n)) & 1u)

static void arm_undefined(gba_t *g, uint32_t op) {
    /* The HLE BIOS stub calls into C with a reserved undefined instruction. */
    if (op == HLE_TRAP_OPCODE && g->hle_bios && g->bus.in_bios) {
        hle_swi(g, g->cpu.r[12] & 0xFF);
        return;
    }
    NOTE(g, NOTE_UNDEFINED, g->cpu.r[15] - 8);
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 4);
}

/* Operand 2 kinds, from the instruction's bits 25 and 6-4 (a constant in each specialised handler below). */
enum { DP_IMM = 0, DP_SHIFT_IMM = 1 /* + type 0-3 */, DP_SHIFT_REG = 5 /* + type 0-3 */, DP_KINDS = 9 };

/* One data-processing instruction; opcode, s and kind are constants in each specialised copy. */
static inline __attribute__((always_inline)) void data_processing(gba_t *g, uint32_t op, uint32_t opcode, bool s,
                                                                  int kind) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t a = c->r[rn];
    bool shift_c = cpu_carry(c);
    uint32_t b;

    if (kind == DP_IMM) {
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        b = ror32(op & 0xFF, rot);
        if (rot) shift_c = (b >> 31) != 0;
    } else if (kind >= DP_SHIFT_REG) {
        /* Register-specified shift: one extra cycle, and PC reads 4 bytes further ahead. */
        uint32_t rm = op & 0xF;
        uint32_t value = c->r[rm] + (rm == 15 ? 4 : 0);
        if (rn == 15) a += 4;
        b = shift_by_reg((uint32_t)(kind - DP_SHIFT_REG), value, c->r[(op >> 8) & 0xF] & 0xFF, &shift_c);
        bus_idle(g, 1);
    } else {
        b = shift_by_imm((uint32_t)(kind - DP_SHIFT_IMM), c->r[op & 0xF], (op >> 7) & 0x1F, &shift_c);
    }

    alu_result_t r = {0, shift_c, cpu_overflow(c)}; /* logical ops: C from shifter, V kept */
    switch (opcode) {
    case 0x0: case 0x8: r.value = a & b; break;        /* AND, TST */
    case 0x1: case 0x9: r.value = a ^ b; break;        /* EOR, TEQ */
    case 0x2: case 0xA: r = alu_add(a, ~b, 1); break;  /* SUB, CMP */
    case 0x3: r = alu_add(b, ~a, 1); break;            /* RSB */
    case 0x4: case 0xB: r = alu_add(a, b, 0); break;   /* ADD, CMN */
    case 0x5: r = alu_add(a, b, cpu_carry(c)); break;  /* ADC */
    case 0x6: r = alu_add(a, ~b, cpu_carry(c)); break; /* SBC */
    case 0x7: r = alu_add(b, ~a, cpu_carry(c)); break; /* RSC */
    case 0xC: r.value = a | b; break;                  /* ORR */
    case 0xD: r.value = b; break;                      /* MOV */
    case 0xE: r.value = a & ~b; break;                 /* BIC */
    default: r.value = ~b; break;                      /* MVN */
    }
    bool is_test = opcode >= 0x8 && opcode <= 0xB;

    if (s && rd == 15) {
        /* S with PC as destination copies SPSR to CPSR (exception return); compares do not branch. */
        if (cpu_has_spsr(c)) cpu_write_cpsr(c, c->spsr[cpu_mode_bank(c->cpsr)]);
    } else if (s) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V)) | (r.value & CPSR_N) |
                  (r.value == 0 ? CPSR_Z : 0) | (r.carry ? CPSR_C : 0) | (r.overflow ? CPSR_V : 0);
    }
    if (!is_test) {
        if (rd == 15) cpu_branch(c, r.value & ((c->cpsr & CPSR_T) ? ~1u : ~3u));
        else c->r[rd] = r.value;
    }
}

/* The specialised copies: dp_handlers[opcode][s][kind]. */
#define DP_FN(o, s, k)     static void dp_##o##_##s##_##k(gba_t *g, uint32_t op) { data_processing(g, op, 0x##o, s, k); }
#define DP_FN_KINDS(o, s) DP_FN(o, s, 0) DP_FN(o, s, 1) DP_FN(o, s, 2) DP_FN(o, s, 3) DP_FN(o, s, 4)     DP_FN(o, s, 5) DP_FN(o, s, 6) DP_FN(o, s, 7) DP_FN(o, s, 8)
#define DP_FN_OP(o) DP_FN_KINDS(o, 0) DP_FN_KINDS(o, 1)
DP_FN_OP(0) DP_FN_OP(1) DP_FN_OP(2) DP_FN_OP(3) DP_FN_OP(4) DP_FN_OP(5) DP_FN_OP(6) DP_FN_OP(7)
DP_FN_OP(8) DP_FN_OP(9) DP_FN_OP(A) DP_FN_OP(B) DP_FN_OP(C) DP_FN_OP(D) DP_FN_OP(E) DP_FN_OP(F)
#define DP_ROW(o, s) {dp_##o##_##s##_0, dp_##o##_##s##_1, dp_##o##_##s##_2, dp_##o##_##s##_3, dp_##o##_##s##_4,                       dp_##o##_##s##_5, dp_##o##_##s##_6, dp_##o##_##s##_7, dp_##o##_##s##_8}
#define DP_OP_ROWS(o) {DP_ROW(o, 0), DP_ROW(o, 1)}
static void (*const dp_handlers[16][2][DP_KINDS])(gba_t *, uint32_t) = {
    DP_OP_ROWS(0), DP_OP_ROWS(1), DP_OP_ROWS(2), DP_OP_ROWS(3), DP_OP_ROWS(4), DP_OP_ROWS(5), DP_OP_ROWS(6), DP_OP_ROWS(7),
    DP_OP_ROWS(8), DP_OP_ROWS(9), DP_OP_ROWS(A), DP_OP_ROWS(B), DP_OP_ROWS(C), DP_OP_ROWS(D), DP_OP_ROWS(E), DP_OP_ROWS(F)};

/* hi = instruction bits 27-20, lo = bits 7-4 of a data-processing instruction: its specialised handler. */
static arm_handler_t dp_handler(uint32_t hi, uint32_t lo) {
    int kind = (hi & 0x20) ? DP_IMM : (lo & 1) ? DP_SHIFT_REG + (int)((lo >> 1) & 3) : DP_SHIFT_IMM + (int)((lo >> 1) & 3);
    return dp_handlers[(hi >> 1) & 0xF][hi & 1][kind];
}

static void arm_mrs(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 12) & 0xF;
    if (BIT(op, 22) && cpu_has_spsr(c)) c->r[rd] = c->spsr[cpu_mode_bank(c->cpsr)];
    else c->r[rd] = c->cpsr;
}

static void arm_msr(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t value = BIT(op, 25) ? ror32(op & 0xFF, ((op >> 8) & 0xF) * 2) : c->r[op & 0xF];
    uint32_t mask = (BIT(op, 19) ? 0xFF000000u : 0) | (BIT(op, 16) ? 0x000000FFu : 0);
    if (BIT(op, 22)) {
        if (cpu_has_spsr(c)) {
            uint32_t *spsr = &c->spsr[cpu_mode_bank(c->cpsr)];
            *spsr = (*spsr & ~mask) | (value & mask);
        }
        return;
    }
    if ((c->cpsr & CPSR_MODE) == MODE_USR) mask &= 0xFF000000u; /* user mode may only write flags */
    uint32_t next = (c->cpsr & ~mask) | (value & mask);
    next = (next & ~CPSR_T) | (c->cpsr & CPSR_T); /* MSR cannot change the Thumb bit */
    cpu_write_cpsr(c, next);
}

static void arm_branch(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t offset = (uint32_t)(((int32_t)(op << 8)) >> 6); /* sign-extended 24-bit word offset */
    if (BIT(op, 24)) c->r[14] = c->r[15] - 4;
    cpu_branch(c, c->r[15] + offset);
}

static void arm_bx(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t target = c->r[op & 0xF];
    if (target & 1) {
        c->cpsr |= CPSR_T;
        cpu_branch(c, target & ~1u);
    } else {
        c->cpsr &= ~CPSR_T;
        cpu_branch(c, target & ~3u);
    }
}

static void arm_swi(gba_t *g, uint32_t op) {
    (void)op;
    cpu_exception(g, 0x08, MODE_SVC, g->cpu.r[15] - 4);
}

static void arm_multiply(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    uint32_t m = c->r[rm], s = c->r[rs], acc = BIT(op, 21) ? c->r[rn] : 0; /* read before rd is written */
    uint32_t result = m * s + acc;
    uint32_t cycles = alu_mul_cycles(s, true) + (BIT(op, 21) ? 1 : 0);
    bus_idle(g, cycles);
    g->bus.data_access = true; /* the next fetch after a multiply is non-sequential, as after a data access */
    g->bus.prefetch_hint = true;
    c->r[rd] = result;
    if (BIT(op, 20)) {
        cpu_set_nz(c, result);
        cpu_set_c(c, mul_carry(MUL_SHORT, m, s, acc)); /* V is left unchanged */
    }
}

static void arm_multiply_long(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t hi = (op >> 16) & 0xF, lo = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    bool is_signed = BIT(op, 22);
    uint32_t m = c->r[rm], s = c->r[rs];
    uint64_t acc = BIT(op, 21) ? ((uint64_t)c->r[hi] << 32 | c->r[lo]) : 0;
    uint64_t result = (is_signed ? (uint64_t)((int64_t)(int32_t)m * (int64_t)(int32_t)s) : (uint64_t)m * s) + acc;
    uint32_t cycles = alu_mul_cycles(s, is_signed) + 1 + (BIT(op, 21) ? 1 : 0);
    bus_idle(g, cycles);
    g->bus.data_access = true; /* the next fetch after a multiply is non-sequential, as after a data access */
    g->bus.prefetch_hint = true;
    c->r[lo] = (uint32_t)result;
    c->r[hi] = (uint32_t)(result >> 32);
    if (BIT(op, 20)) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | ((uint32_t)(result >> 32) & CPSR_N) | (result == 0 ? CPSR_Z : 0);
        cpu_set_c(c, mul_carry(is_signed ? MUL_LONG_SIGNED : MUL_LONG_UNSIGNED, m, s, acc));
    }
}

static void arm_single_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t offset;
    if (BIT(op, 25)) {
        bool unused_carry = cpu_carry(c); /* RRX reads C; the carry-out is discarded */
        offset = shift_by_imm((op >> 5) & 3, c->r[op & 0xF], (op >> 7) & 0x1F, &unused_carry);
    } else {
        offset = op & 0xFFF;
    }
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);

    if (BIT(op, 20)) {
        uint32_t value = BIT(op, 22) ? bus_read8(g, addr, false) : cpu_load_word(g, addr);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved; /* a load into the base register wins */
        if (rd == 15) cpu_branch(c, value & ~3u);
        else c->r[rd] = value;
    } else {
        uint32_t value = c->r[rd] + (rd == 15 ? 4 : 0); /* STR PC stores address + 12 */
        if (BIT(op, 22)) bus_write8(g, addr, (uint8_t)value, false);
        else bus_write32(g, addr, value, false);
        if (writeback) c->r[rn] = moved;
    }
}

static void arm_halfword_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t offset = BIT(op, 22) ? (((op >> 4) & 0xF0) | (op & 0xF)) : c->r[op & 0xF];
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);
    uint32_t sh = (op >> 5) & 3;

    if (BIT(op, 20)) {
        uint32_t value = sh == 1   ? cpu_load_half(g, addr)
                         : sh == 2 ? cpu_load_signed_byte(g, addr)
                                   : cpu_load_signed_half(g, addr);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved;
        if (rd == 15) cpu_branch(c, value & ~3u);
        else c->r[rd] = value;
    } else if (sh == 1) {
        uint32_t value = c->r[rd] + (rd == 15 ? 4 : 0);
        bus_write16(g, addr, (uint16_t)value, false);
        if (writeback) c->r[rn] = moved;
    }
    /* sh 2/3 with L = 0 are ARMv5 LDRD/STRD: no effect on the ARM7TDMI. */
}

static void arm_swap(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF, rm = op & 0xF;
    uint32_t addr = c->r[rn], source = c->r[rm], loaded;
    if (BIT(op, 22)) {
        loaded = bus_read8(g, addr, false);
        bus_write8(g, addr, (uint8_t)source, false);
    } else {
        loaded = cpu_load_word(g, addr);
        bus_write32(g, addr, source, false);
    }
    bus_idle(g, 1);
    c->r[rd] = loaded;
}

static void arm_block_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF;
    uint32_t rlist = op & 0xFFFF;
    bool pre = BIT(op, 24), up = BIT(op, 23), psr = BIT(op, 22), wb = BIT(op, 21), load = BIT(op, 20);

    /* ARMv4 quirk: an empty list transfers only PC but moves the base as if 16 registers were listed. */
    uint32_t count = (uint32_t)__builtin_popcount(rlist);
    if (rlist == 0) {
        rlist = 1u << 15;
        count = 16;
    }

    uint32_t base = c->r[rn];
    uint32_t bytes = count * 4;
    uint32_t new_base = up ? base + bytes : base - bytes;
    uint32_t addr = up ? base + (pre ? 4 : 0) : base - bytes + (pre ? 0 : 4);

    /* The S bit without a PC load transfers the user-mode registers. */
    bool user_bank = psr && !(load && (rlist & 0x8000));
    uint32_t mode = c->cpsr & CPSR_MODE;
    bool seq = false;

    if (load) {
        if (wb) c->r[rn] = new_base; /* a loaded base register overrides the writeback */
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | MODE_USR);
        for (uint32_t i = 0; i < 15; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | mode);
        bool load_pc = (rlist & 0x8000) != 0;
        uint32_t pc = load_pc ? bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0) : 0;
        bus_idle(g, 1);
        if (load_pc) {
            if (psr && cpu_has_spsr(c)) cpu_write_cpsr(c, c->spsr[cpu_mode_bank(c->cpsr)]);
            cpu_branch(c, pc & ((c->cpsr & CPSR_T) ? ~1u : ~3u));
        }
    } else {
        /* A stored base is the original value only when it is the lowest register listed. */
        bool base_first = (rlist & ((1u << rn) - 1)) == 0;
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | MODE_USR);
        for (uint32_t i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value;
            if (i == 15) value = c->r[15] + 4; /* STM PC stores address + 12 */
            else if (i == rn) value = (base_first || !wb) ? base : new_base;
            else value = c->r[i];
            bus_write32(g, addr, value, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | mode);
        if (wb) c->r[rn] = new_base;
    }
}

/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if (hi == 0x12 && lo == 0x1) return arm_bx;
    if ((hi & 0xFC) == 0x00 && lo == 0x9) return arm_multiply;
    if ((hi & 0xF8) == 0x08 && lo == 0x9) return arm_multiply_long;
    if ((hi & 0xFB) == 0x10 && lo == 0x9) return arm_swap;
    if ((hi & 0xE0) == 0x00 && lo == 0x9) return arm_undefined;
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_halfword_transfer; /* lo = 1011, 1101, 1111 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return dp_handler(hi, lo);
    if ((hi & 0xE0) == 0x60 && (lo & 0x1)) return arm_undefined;
    if ((hi & 0xC0) == 0x40) return arm_single_transfer;
    if ((hi & 0xE0) == 0x80) return arm_block_transfer;
    if ((hi & 0xE0) == 0xA0) return arm_branch;
    if ((hi & 0xF0) == 0xF0) return arm_swi;
    return arm_undefined; /* coprocessor space: the GBA has no coprocessors */
}

static void arm_fast_init_table(void);

int arm_class(uint32_t i) {
    arm_handler_t h = arm_table[i];
    if ((i >> 4 & 0xC0) == 0x00 && h == dp_handler(i >> 4, i & 0xF)) return ARM_CLASS_DP;
    if (h == arm_branch) return ARM_CLASS_BRANCH;
    if (h == arm_single_transfer) return ARM_CLASS_SINGLE;
    if (h == arm_block_transfer) return ARM_CLASS_BLOCK;
    if (h == arm_multiply) return ARM_CLASS_MUL;
    if (h == arm_halfword_transfer) return ARM_CLASS_HALF;
    if (h == arm_bx) return ARM_CLASS_BX;
    return ARM_CLASS_OTHER;
}

void arm_init_table(void) {
    for (uint32_t i = 0; i < 4096; i++) arm_table[i] = arm_classify(i >> 4, i & 0xF);
    arm_fast_init_table();
}

void arm_execute(gba_t *g, uint32_t op) {
    arm_table[((op >> 16) & 0xFF0) | ((op >> 4) & 0xF)](g, op);
}

/* ---- The threaded loop's ARM handlers (cpu_fast.h) ---- */

arm_fast_t *arm_fast_table[4096];

void arm_fast_skip(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_fetched(g, pc, op, seq);
    ARM_NEXT(g, pc + 4, true);
}

void arm_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    fast_generic_begin(g);
    arm_fetched(g, pc, op, seq);
    arm_execute(g, op);
    if (!fast_generic_end(g, pc + 4) || (g->cpu.cpsr & CPSR_T)) return; /* r15 and next_fetch_seq are stored */
    ARM_NEXT(g, g->cpu.r[15], g->cpu.next_fetch_seq);
}

/* Data processing that does not write PC. */
#define DPF_FN(o, s, k)                                                                     \
    static void dpf_##o##_##s##_##k(gba_t *g, uint32_t pc, uint32_t op, bool seq) {        \
        if (((op >> 12) & 0xF) == 15) FAST_TAIL return arm_fast_generic(g, pc, op, seq);    \
        arm_fetched(g, pc, op, seq);                                                        \
        data_processing(g, op, 0x##o, s, k);                                                \
        ARM_NEXT(g, pc + 4, true);                                                          \
    }
#define DPF_FN_KINDS(o, s) DPF_FN(o, s, 0) DPF_FN(o, s, 1) DPF_FN(o, s, 2) DPF_FN(o, s, 3) DPF_FN(o, s, 4) \
    DPF_FN(o, s, 5) DPF_FN(o, s, 6) DPF_FN(o, s, 7) DPF_FN(o, s, 8)
#define DPF_FN_OP(o) DPF_FN_KINDS(o, 0) DPF_FN_KINDS(o, 1)
DPF_FN_OP(0) DPF_FN_OP(1) DPF_FN_OP(2) DPF_FN_OP(3) DPF_FN_OP(4) DPF_FN_OP(5) DPF_FN_OP(6) DPF_FN_OP(7)
DPF_FN_OP(8) DPF_FN_OP(9) DPF_FN_OP(A) DPF_FN_OP(B) DPF_FN_OP(C) DPF_FN_OP(D) DPF_FN_OP(E) DPF_FN_OP(F)
#define DPF_ROW(o, s) {dpf_##o##_##s##_0, dpf_##o##_##s##_1, dpf_##o##_##s##_2, dpf_##o##_##s##_3, dpf_##o##_##s##_4, \
                       dpf_##o##_##s##_5, dpf_##o##_##s##_6, dpf_##o##_##s##_7, dpf_##o##_##s##_8}
#define DPF_OP_ROWS(o) {DPF_ROW(o, 0), DPF_ROW(o, 1)}
static arm_fast_t *const dpf_handlers[16][2][DP_KINDS] = {
    DPF_OP_ROWS(0), DPF_OP_ROWS(1), DPF_OP_ROWS(2), DPF_OP_ROWS(3), DPF_OP_ROWS(4), DPF_OP_ROWS(5), DPF_OP_ROWS(6),
    DPF_OP_ROWS(7), DPF_OP_ROWS(8), DPF_OP_ROWS(9), DPF_OP_ROWS(A), DPF_OP_ROWS(B), DPF_OP_ROWS(C), DPF_OP_ROWS(D),
    DPF_OP_ROWS(E), DPF_OP_ROWS(F)};

/* B / BL */
static void arm_fast_branch(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_fetched(g, pc, op, seq);
    uint32_t offset = (uint32_t)(((int32_t)(op << 8)) >> 6);
    if (BIT(op, 24)) g->cpu.r[14] = pc + 4;
    uint32_t target = pc + 8 + offset;
    fast_refill(g, target, 4);
    ARM_NEXT(g, target, true);
}

/* LDR / STR / LDRB / STRB with RAM, or LDR / LDRB from the ROM; not PC. */
static void arm_fast_single(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    c->r[15] = pc + 8; /* the base or the offset register may be PC */
    uint32_t offset;
    if (BIT(op, 25)) {
        bool unused_carry = cpu_carry(c);
        offset = shift_by_imm((op >> 5) & 3, c->r[op & 0xF], (op >> 7) & 0x1F, &unused_carry);
    } else {
        offset = op & 0xFFF;
    }
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);
    bool load = BIT(op, 20), byte = BIT(op, 22);
    unsigned size = byte ? 1 : 4;
    uint32_t a = byte ? addr : addr & ~3u;
    uint8_t *p = rd == 15 ? NULL : fast_data_ptr(g, a, size, !load);
    if (!p || !fast_data_ok(g, bus_access_cycles(g, pc & ~3u, 4, seq))) FAST_TAIL return arm_fast_generic(g, pc, op, seq);
    arm_fetched(g, pc, op, seq);
    fast_data_begin(g);
    fast_data_cycles(g, a, size);
    if (load) {
        uint32_t value = byte ? p[0] : ror32(bus_load_n(p, 4), (addr & 3) * 8);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved; /* a load into the base register wins */
        c->r[rd] = value;
    } else {
        uint32_t value = c->r[rd];
        if (byte) p[0] = (uint8_t)value;
        else bus_store32(p, value);
        if (writeback) c->r[rn] = moved;
    }
    ARM_NEXT(g, pc + 4, fast_after_data(g, pc + 4));
}

/* LDM / STM with RAM, without the S bit or an empty list. */
static void arm_fast_block(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rlist = op & 0xFFFF;
    bool pre = BIT(op, 24), up = BIT(op, 23), wb = BIT(op, 21), load = BIT(op, 20);
    if (BIT(op, 22) || rlist == 0 || rn == 15) FAST_TAIL return arm_fast_generic(g, pc, op, seq);
    uint32_t count = (uint32_t)__builtin_popcount(rlist), base = c->r[rn], bytes = count * 4;
    uint32_t new_base = up ? base + bytes : base - bytes;
    uint32_t addr = up ? base + (pre ? 4 : 0) : base - bytes + (pre ? 0 : 4);
    uint32_t mask, cyc;
    uint8_t *mem = fast_block_ptr(g, addr, count, bus_access_cycles(g, pc & ~3u, 4, seq), &mask, &cyc);
    if (!mem) FAST_TAIL return arm_fast_generic(g, pc, op, seq);
    arm_fetched(g, pc, op, seq);
    fast_data_begin(g);
    g->bus.prefetch_hint = true;
    if (load) {
        if (wb) c->r[rn] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 15; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = fast_block_load(g, mem, mask, cyc, addr);
            addr += 4;
        }
        uint32_t target = (rlist & 0x8000) ? fast_block_load(g, mem, mask, cyc, addr) & ~3u : 0;
        bus_idle(g, 1);
        if (rlist & 0x8000) {
            fast_refill(g, target, 4);
            ARM_NEXT(g, target, true);
        }
    } else {
        bool base_first = (rlist & ((1u << rn) - 1)) == 0; /* a stored base is the original value only then */
        for (uint32_t i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value = i == 15 ? pc + 12 : i == rn ? ((base_first || !wb) ? base : new_base) : c->r[i];
            fast_block_store(g, mem, mask, cyc, addr, value);
            addr += 4;
        }
        if (wb) c->r[rn] = new_base;
    }
    ARM_NEXT(g, pc + 4, fast_after_data(g, pc + 4));
}

static void arm_fast_enter(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    (void)op;
    ARM_NEXT(g, pc, seq);
}

void arm_fast_run(gba_t *g) {
    arm_fast_enter(g, g->cpu.r[15], 0, g->cpu.next_fetch_seq);
}

static void arm_fast_init_table(void) {
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t hi = i >> 4, lo = i & 0xF;
        arm_handler_t h = arm_table[i];
        arm_fast_t *f = arm_fast_generic;
        if ((hi & 0xC0) == 0x00 && h == dp_handler(hi, lo)) {
            int kind = (hi & 0x20) ? DP_IMM : (lo & 1) ? DP_SHIFT_REG + (int)((lo >> 1) & 3) : DP_SHIFT_IMM + (int)((lo >> 1) & 3);
            f = dpf_handlers[(hi >> 1) & 0xF][hi & 1][kind];
        } else if (h == arm_branch) {
            f = arm_fast_branch;
        } else if (h == arm_single_transfer) {
            f = arm_fast_single;
        } else if (h == arm_block_transfer) {
            f = arm_fast_block;
        }
        arm_fast_table[i] = f;
    }
}
