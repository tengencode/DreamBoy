#ifndef GBA_CPU_H
#define GBA_CPU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

enum {
    MODE_USR = 0x10,
    MODE_FIQ = 0x11,
    MODE_IRQ = 0x12,
    MODE_SVC = 0x13,
    MODE_ABT = 0x17,
    MODE_UND = 0x1B,
    MODE_SYS = 0x1F,
};

#define CPSR_N (1u << 31)
#define CPSR_Z (1u << 30)
#define CPSR_C (1u << 29)
#define CPSR_V (1u << 28)
#define CPSR_I (1u << 7)
#define CPSR_F (1u << 6)
#define CPSR_T (1u << 5)
#define CPSR_MODE 0x1Fu

enum { BANK_USR, BANK_FIQ, BANK_IRQ, BANK_SVC, BANK_ABT, BANK_UND, BANK_COUNT };

/*
 * r[15]: between instructions, the address of the next instruction. While an
 * instruction executes, its address + 8 (ARM) or + 4 (Thumb) — the value the
 * program reads as PC.
 */
typedef struct {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t spsr[BANK_COUNT]; /* spsr[BANK_USR] is unused */
    uint32_t bank_r13[BANK_COUNT];
    uint32_t bank_r14[BANK_COUNT];
    uint32_t bank_usr_r8_12[5]; /* r8-r12 of every mode except FIQ */
    uint32_t bank_fiq_r8_12[5];
    bool branched;       /* the executing instruction wrote PC */
    bool next_fetch_seq; /* the next fetch continues sequentially */
} arm_cpu_t;

void cpu_init_tables(void); /* builds the ARM and Thumb decode tables; idempotent */
void cpu_reset(gba_t *g);   /* registers zero, SVC mode, IRQ+FIQ masked, ARM state, PC = 0 */
void cpu_step(gba_t *g);    /* take a pending enabled IRQ, then execute one instruction */
/* cpu_step again and again until the next event is due, or the CPU halts or stops: on arm64 the assembly loop
 * (core/arch/arm64), elsewhere (and with GBA_FORCE_GENERIC) its plain-C twin cpu_run_generic. */
void cpu_run(gba_t *g);
void cpu_run_generic(gba_t *g);
#ifdef GBA_ARM64
void a64_cpu_run(gba_t *g);
#endif

int cpu_mode_bank(uint32_t mode);
bool cpu_has_spsr(const arm_cpu_t *c);
void cpu_write_cpsr(arm_cpu_t *c, uint32_t value); /* swaps banked registers on mode change */
void cpu_branch(arm_cpu_t *c, uint32_t target);
void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr);
static inline bool cpu_cond_passed(uint32_t cpsr, uint32_t cond) {
    bool n = (cpsr & CPSR_N) != 0, z = (cpsr & CPSR_Z) != 0;
    bool cf = (cpsr & CPSR_C) != 0, v = (cpsr & CPSR_V) != 0;
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return cf;
    case 0x3: return !cf;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return cf && !z;
    case 0x9: return !cf || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    case 0xE: return true;
    default: return false; /* NV: never executes on ARMv4 */
    }
}

static inline void cpu_set_nz(arm_cpu_t *c, uint32_t result) {
    c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | (result & CPSR_N) | (result == 0 ? CPSR_Z : 0);
}

static inline void cpu_set_c(arm_cpu_t *c, bool on) {
    c->cpsr = on ? c->cpsr | CPSR_C : c->cpsr & ~CPSR_C;
}

static inline void cpu_set_v(arm_cpu_t *c, bool on) {
    c->cpsr = on ? c->cpsr | CPSR_V : c->cpsr & ~CPSR_V;
}

static inline bool cpu_carry(const arm_cpu_t *c) {
    return (c->cpsr & CPSR_C) != 0;
}

static inline bool cpu_overflow(const arm_cpu_t *c) {
    return (c->cpsr & CPSR_V) != 0;
}

/* arm.c */
void arm_init_table(void);
/* What the ARM decode table holds at index i (bits 27-20 << 4 | bits 7-4), for the assembly loop's table. */
enum { ARM_CLASS_OTHER, ARM_CLASS_DP, ARM_CLASS_BRANCH, ARM_CLASS_SINGLE, ARM_CLASS_BLOCK, ARM_CLASS_MUL, ARM_CLASS_HALF,
       ARM_CLASS_BX };
int arm_class(uint32_t i);
void arm_execute(gba_t *g, uint32_t op);
/* thumb.c */
void thumb_init_table(void);
/* The Thumb format at index i (bits 15-6), for the assembly loop's table. */
enum {
    THUMB_CLASS_OTHER, THUMB_CLASS_SHIFT_IMM, THUMB_CLASS_ADD_SUB, THUMB_CLASS_IMM, THUMB_CLASS_ALU, THUMB_CLASS_HI_REG,
    THUMB_CLASS_PC_LOAD, THUMB_CLASS_REG_OFFSET, THUMB_CLASS_REG_OFFSET_SIGNED, THUMB_CLASS_IMM_OFFSET,
    THUMB_CLASS_HALF_IMM, THUMB_CLASS_SP_RELATIVE, THUMB_CLASS_LOAD_ADDRESS, THUMB_CLASS_SP_ADJUST,
    THUMB_CLASS_PUSH_POP, THUMB_CLASS_BLOCK, THUMB_CLASS_COND_BRANCH, THUMB_CLASS_BRANCH, THUMB_CLASS_BL_HIGH,
    THUMB_CLASS_BL_LOW
};
int thumb_class(uint32_t i);
void thumb_execute(gba_t *g, uint16_t op);

#endif
