/*
 * Milestone 7b: the CPU loops against each other on random instruction streams. The threaded C loop
 * (cpu_run_generic) must match cpu_step run one instruction at a time, and on arm64 the assembly loop
 * (a64_cpu_run) must match the C loop: every register, flag, mode, memory byte, I/O register and cycle.
 */
#include <string.h>

#include "cpu/cpu.h"
#include "gba_internal.h"
#include "test.h"
#include "testutil.h"

static uint32_t rng;

static uint32_t rnd(void) { /* xorshift32: the same streams on every run */
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t pick(uint32_t n) {
    return rnd() % n;
}

#define CODE 0x03000000u   /* the random program: 0x1000 bytes of IWRAM */
#define CODE_SIZE 0x1000u
#define DATA_IW 0x03004000u /* where the base registers point */
#define DATA_EW 0x02010000u

/* A condition: mostly always, sometimes any. */
static uint32_t cond(void) {
    return pick(4) ? 0xEu << 28 : pick(16) << 28;
}

/* An ARM instruction from the classes the loops treat differently. Data-processing results go to r0-r7, so the
 * base registers r8-r12 keep pointing at RAM (or the ROM) most of the time. */
static uint32_t arm_op(void) {
    uint32_t c = cond(), k = pick(100);
    uint32_t rd = pick(8), rn = pick(16), rm = pick(16), base = 8 + pick(5);
    if (k < 40) { /* data processing */
        uint32_t opc = pick(16), s = pick(2), op2;
        if (opc >= 8 && opc <= 11) s = 1; /* TST..CMN without S are other instructions */
        switch (pick(3)) {
        case 0: op2 = (1u << 25) | pick(16) << 8 | pick(256); break;                     /* immediate */
        case 1: op2 = pick(32) << 7 | pick(4) << 5 | rm; break;                            /* shifted by an immediate */
        default: op2 = pick(8) << 8 | pick(4) << 5 | 1u << 4 | rm; break;                  /* shifted by a register */
        }
        return c | opc << 21 | s << 20 | rn << 16 | rd << 12 | op2;
    }
    if (k < 62) { /* LDR / STR (B) */
        uint32_t off = pick(3) ? pick(64) : pick(4096);
        uint32_t reg = pick(4) ? 0 : (1u << 25 | pick(32) << 7 | pick(4) << 5 | pick(8)); /* or a shifted register */
        uint32_t pre = pick(4) ? 1 : 0, wb = pre ? pick(4) == 0 : 0;
        return c | 1u << 26 | (reg ? reg : off) | pre << 24 | pick(2) << 23 | pick(2) << 22 | wb << 21 |
               pick(2) << 20 | base << 16 | rd << 12;
    }
    if (k < 70) { /* LDRH / STRH / LDRSB / LDRSH */
        uint32_t sh = 1 + pick(3), load = sh == 1 ? pick(2) : 1;
        return c | 1u << 24 | pick(2) << 23 | 1u << 22 | load << 20 | base << 16 | rd << 12 | pick(16) << 8 |
               1u << 7 | sh << 5 | 1u << 4 | pick(16);
    }
    if (k < 80) { /* LDM / STM */
        uint32_t list = pick(0x10000) & ~(0x1Fu << 8); /* not the base registers */
        if (pick(8) == 0) list |= 1u << base;
        if (pick(8)) list &= 0x7FFF; /* mostly without PC */
        if (!list) list = 1;
        return c | 4u << 25 | pick(2) << 24 | pick(2) << 23 | (pick(10) == 0) << 22 | pick(2) << 21 |
               pick(2) << 20 | base << 16 | list;
    }
    if (k < 88) { /* B / BL, nearby */
        int32_t off = (int32_t)pick(40) - 20;
        return c | 5u << 25 | pick(2) << 24 | ((uint32_t)off & 0xFFFFFF);
    }
    if (k < 93) { /* MUL / MLA / long */
        if (pick(2)) return c | pick(2) << 21 | pick(2) << 20 | rd << 16 | pick(8) << 12 | pick(8) << 8 | 9u << 4 | pick(8);
        return c | 1u << 23 | pick(4) << 21 | pick(2) << 20 | rd << 16 | ((rd + 1) & 7) << 12 | pick(8) << 8 | 9u << 4 | pick(8);
    }
    if (k < 96) { /* MRS / MSR (flags) */
        if (pick(2)) return c | 0x010F0000u | rd << 12;
        return c | 0x0328F000u | pick(16) << 8 | pick(256); /* MSR CPSR_f, #imm */
    }
    if (k < 98) return c | 0x01000090u | pick(2) << 22 | base << 16 | rd << 12 | pick(8); /* SWP */
    return rnd(); /* anything at all */
}

/* A Thumb instruction: ALU results go to r0-r3, r4-r7 hold RAM addresses. */
static uint16_t thumb_op(void) {
    uint32_t k = pick(100), rd = pick(4), rs = pick(8), base = 4 + pick(4);
    if (k < 10) return (uint16_t)(pick(3) << 11 | pick(32) << 6 | rs << 3 | rd);                /* shift by imm */
    if (k < 16) return (uint16_t)(0x1800 | pick(4) << 9 | pick(8) << 6 | rs << 3 | rd);          /* add/sub */
    if (k < 26) return (uint16_t)(0x2000 | pick(4) << 11 | rd << 8 | pick(256));                 /* imm8 */
    if (k < 38) return (uint16_t)(0x4000 | pick(16) << 6 | rs << 3 | rd);                        /* ALU */
    if (k < 42) return (uint16_t)(0x4400 | pick(3) << 8 | pick(2) << 7 | pick(16) << 3 | rd);    /* hi registers */
    if (k < 46) return (uint16_t)(0x4800 | rd << 8 | pick(256));                                 /* LDR [PC] */
    if (k < 52) return (uint16_t)(0x5000 | pick(8) << 9 | pick(4) << 6 | base << 3 | rd);        /* [Rb, Ro] */
    if (k < 62) return (uint16_t)(0x6000 | pick(4) << 11 | pick(32) << 6 | base << 3 | rd);      /* [Rb, #imm] */
    if (k < 66) return (uint16_t)(0x8000 | pick(2) << 11 | pick(32) << 6 | base << 3 | rd);      /* halfwords */
    if (k < 72) return (uint16_t)(0x9000 | pick(2) << 11 | rd << 8 | pick(256));                 /* [SP, #imm] */
    if (k < 75) return (uint16_t)(0xA000 | pick(2) << 11 | rd << 8 | pick(256));                 /* ADD PC/SP */
    if (k < 78) return (uint16_t)(0xB000 | pick(256));                                           /* ADD SP */
    if (k < 84) return (uint16_t)(0xB400 | pick(2) << 11 | pick(2) << 8 | pick(256));            /* PUSH / POP */
    if (k < 87) return (uint16_t)(0xC000 | pick(2) << 11 | base << 8 | (pick(16) & 0x0F));       /* LDMIA / STMIA */
    if (k < 93) return (uint16_t)(0xD000 | pick(14) << 8 | ((uint32_t)((int32_t)pick(24) - 12) & 0xFF)); /* B<cond> */
    if (k < 96) return (uint16_t)(0xE000 | ((uint32_t)((int32_t)pick(24) - 12) & 0x7FF));      /* B */
    if (k < 98) return (uint16_t)(0xF000 | (pick(2) ? 0x800 : 0) | pick(8));                     /* BL halves */
    return (uint16_t)rnd();
}

/* Where a base register points: mostly RAM, else the ROM, the palette, VRAM (also its mirrored top) or OAM. */
static uint32_t data_pointer(bool rom) {
    switch (pick(10)) {
    case 0: return DATA_EW + pick(0x1000);
    case 1: return rom ? 0x08000000u + pick(0x100) : DATA_EW + pick(0x1000);
    case 2: return 0x05000000u + pick(0x380);
    case 3: return 0x06000000u + (pick(2) ? 0x17C00u + pick(0x400) : pick(0x1000)); /* also across 0x18000 */
    case 4: return 0x07000000u + pick(0x380);
    default: return DATA_IW + pick(0x1000);
    }
}

#define ROM_CODE 0x400u /* the program's place in the ROM, when it runs from there */

/* A machine with a random program at CODE (in IWRAM, or in the ROM: the cartridge's wait states and prefetch
 * buffer), random registers and flags, bases into RAM and the ROM. */
static gba_t *machine(uint32_t seed, bool thumb, bool in_rom) {
    static uint32_t rom[(ROM_CODE + CODE_SIZE) / 4];
    static uint8_t code[CODE_SIZE];
    rng = seed;
    for (uint32_t a = 0; a < CODE_SIZE; a += thumb ? 2 : 4) {
        uint32_t op = thumb ? thumb_op() : arm_op();
        for (uint32_t b = 0; b < (thumb ? 2u : 4u); b++) code[a + b] = (uint8_t)(op >> (8 * b));
    }
    rom[0] = 0xEAFFFFFE; /* b . */
    for (uint32_t i = 1; i < ROM_CODE / 4; i++) rom[i] = rnd();
    memcpy(&rom[ROM_CODE / 4], code, CODE_SIZE);
    gba_t *g = tu_arm(rom, TU_LEN(rom));
    if (!in_rom) memcpy(g->bus.iwram, code, CODE_SIZE);
    for (uint32_t i = 0; i < 0x2000; i++) g->bus.iwram[0x4000 + i] = (uint8_t)rnd();
    for (uint32_t i = 0; i < 0x2000; i++) g->bus.ewram[0x10000 + i] = (uint8_t)rnd();
    arm_cpu_t *c = &g->cpu;
    for (int i = 0; i < 8; i++) c->r[i] = rnd();
    for (int i = 8; i < 13; i++) c->r[i] = data_pointer(true);
    if (thumb)
        for (int i = 4; i < 8; i++) c->r[i] = data_pointer(false);
    c->r[13] = DATA_IW + 0x1800 + pick(0x400);
    uint32_t start = in_rom ? 0x08000000u + ROM_CODE : CODE;
    c->r[14] = start + pick(CODE_SIZE / 4) * 4 + (thumb ? 1 : 0);
    c->r[15] = start;
    c->cpsr = (rnd() & 0xF0000000u) | MODE_SYS | (thumb ? CPSR_T : 0);
    c->next_fetch_seq = false;
    if (pick(2)) bus_set_waitcnt(g, (uint16_t)(rnd() & 0x7FFF)); /* other wait states, the prefetch buffer */
    return g;
}

/* Runs the machine for about `cycles` with `run` for the CPU and the events in between (as run_frame). */
static void run_for(gba_t *g, uint64_t cycles, void (*run)(gba_t *)) {
    uint64_t end = g->now + cycles;
    while (g->now < end && !g->irq.stopped) {
        while (g->now < sched_next(&g->sched) && !g->irq.stopped) {
            if (g->irq.halted) {
                if (!irq_wake(g)) {
                    uint64_t next = sched_next(&g->sched);
                    if (g->irq.pending && irq_wake_at(g) < next) {
                        g->now = irq_wake_at(g);
                        continue;
                    }
                    g->now = next;
                    break;
                }
                g->irq.halted = false;
            }
            run(g);
        }
        gba_run_events(g);
    }
}

/* The CPU as it was before 7b: one cpu_step at a time. */
static void run_by_steps(gba_t *g) {
    while (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) cpu_step(g);
}

/* Everything an instruction can change, except the per-instruction scratch flags (branched, data_access, ...). */
static bool same(const gba_t *a, const gba_t *b) {
    const arm_cpu_t *x = &a->cpu, *y = &b->cpu;
    bool ok = !memcmp(x->r, y->r, sizeof x->r) && x->cpsr == y->cpsr && !memcmp(x->spsr, y->spsr, sizeof x->spsr) &&
              !memcmp(x->bank_r13, y->bank_r13, sizeof x->bank_r13) && !memcmp(x->bank_r14, y->bank_r14, sizeof x->bank_r14) &&
              !memcmp(x->bank_usr_r8_12, y->bank_usr_r8_12, sizeof x->bank_usr_r8_12) &&
              !memcmp(x->bank_fiq_r8_12, y->bank_fiq_r8_12, sizeof x->bank_fiq_r8_12) && x->next_fetch_seq == y->next_fetch_seq;
    ok = ok && a->now == b->now && !memcmp(a->bus.ewram, b->bus.ewram, EWRAM_SIZE) &&
         !memcmp(a->bus.iwram, b->bus.iwram, IWRAM_SIZE) && !memcmp(a->bus.vram, b->bus.vram, VRAM_SIZE) &&
         !memcmp(a->bus.pal, b->bus.pal, PAL_SIZE) && !memcmp(a->bus.oam, b->bus.oam, OAM_SIZE);
    ok = ok && a->bus.open_bus == b->bus.open_bus && a->bus.in_bios == b->bus.in_bios &&
         a->bus.bios_latch == b->bus.bios_latch && a->bus.prefetch_end == b->bus.prefetch_end &&
         a->bus.waitcnt == b->bus.waitcnt && !memcmp(&a->io, &b->io, sizeof a->io) &&
         !memcmp(&a->irq, &b->irq, sizeof a->irq) && !memcmp(&a->dma, &b->dma, sizeof a->dma) &&
         !memcmp(&a->timers, &b->timers, sizeof a->timers);
    return ok;
}

static void compare(bool thumb, void (*run_a)(gba_t *), void (*run_b)(gba_t *), int programs) {
    int bad = 0;
    for (int p = 0; p < programs; p++) {
        uint32_t seed = 0x9E3779B9u * (uint32_t)(p + 1) + (thumb ? 7 : 0);
        bool in_rom = p % 2 == 1;
        gba_t *a = machine(seed, thumb, in_rom), *b = machine(seed, thumb, in_rom);
        bool ok = true;
        for (int slice = 0; slice < 8 && ok; slice++) { /* compared every slice: a difference shows near its start */
            run_for(a, 3000, run_a);
            run_for(b, 3000, run_b);
            ok = same(a, b);
        }
        if (!ok && bad++ < 3)
            fprintf(stderr, "  program %d (%s%s): pc %08x/%08x now %llu/%llu cpsr %08x/%08x\n", p, thumb ? "Thumb" : "ARM", in_rom ? ", ROM" : "",
                    a->cpu.r[15], b->cpu.r[15], (unsigned long long)a->now, (unsigned long long)b->now, a->cpu.cpsr,
                    b->cpu.cpsr);
        gba_destroy(a);
        gba_destroy(b);
    }
    CHECK_EQ(bad, 0);
}

TEST(twins_c_loop_matches_cpu_step_arm) {
    compare(false, run_by_steps, cpu_run_generic, 150);
}

TEST(twins_c_loop_matches_cpu_step_thumb) {
    compare(true, run_by_steps, cpu_run_generic, 150);
}

#ifdef GBA_ARM64
TEST(twins_asm_loop_matches_c_loop_arm) {
    compare(false, cpu_run_generic, a64_cpu_run, 400);
}

TEST(twins_asm_loop_matches_c_loop_thumb) {
    compare(true, cpu_run_generic, a64_cpu_run, 400);
}
#endif
