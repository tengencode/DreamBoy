#include "cpu/cpu.h"

#include <string.h>

#include "gba_internal.h"
#include "mem/bus_inline.h"
#include "cpu/cpu_fast.h"

/* cpu_cond_table[cond] bit n: the condition passes when the flags NZCV are n (built by cpu_init_tables). */
__attribute__((visibility("hidden"))) uint16_t cpu_cond_table[16];

int cpu_mode_bank(uint32_t mode) {
    switch (mode & CPSR_MODE) {
    case MODE_FIQ: return BANK_FIQ;
    case MODE_IRQ: return BANK_IRQ;
    case MODE_SVC: return BANK_SVC;
    case MODE_ABT: return BANK_ABT;
    case MODE_UND: return BANK_UND;
    default: return BANK_USR; /* USR, SYS and invalid mode values */
    }
}

bool cpu_has_spsr(const arm_cpu_t *c) {
    return cpu_mode_bank(c->cpsr) != BANK_USR;
}

static void switch_bank(arm_cpu_t *c, int from, int to) {
    if (from == to) return;
    if (from == BANK_FIQ || to == BANK_FIQ) {
        memcpy(from == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, &c->r[8], sizeof c->bank_usr_r8_12);
        memcpy(&c->r[8], to == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, sizeof c->bank_usr_r8_12);
    }
    c->bank_r13[from] = c->r[13];
    c->bank_r14[from] = c->r[14];
    c->r[13] = c->bank_r13[to];
    c->r[14] = c->bank_r14[to];
}

void cpu_write_cpsr(arm_cpu_t *c, uint32_t value) {
    switch_bank(c, cpu_mode_bank(c->cpsr), cpu_mode_bank(value));
    c->cpsr = value;
}

void cpu_branch(arm_cpu_t *c, uint32_t target) {
    c->r[15] = target;
    c->branched = true;
    c->next_fetch_seq = false;
}

void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr) {
    arm_cpu_t *c = &g->cpu;
    uint32_t old = c->cpsr;
    cpu_write_cpsr(c, (old & ~(CPSR_MODE | CPSR_T)) | mode | CPSR_I);
    c->spsr[cpu_mode_bank(mode)] = old;
    c->r[14] = return_addr;
    cpu_branch(c, vector);
}

void cpu_init_tables(void) {
    for (uint32_t cond = 0; cond < 16; cond++) {
        cpu_cond_table[cond] = 0;
        for (uint32_t nzcv = 0; nzcv < 16; nzcv++)
            if (cpu_cond_passed(nzcv << 28, cond)) cpu_cond_table[cond] |= (uint16_t)(1u << nzcv);
    }
    arm_init_table();
    thumb_init_table();
}

void cpu_reset(gba_t *g) {
    memset(&g->cpu, 0, sizeof g->cpu);
    g->cpu.cpsr = MODE_SVC | CPSR_I | CPSR_F;
}

static inline void step(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    /* LR_irq = next instruction + 4, so the handler's "subs pc, lr, #4" resumes it. */
    if (g->irq.pending && irq_line(g) && !(c->cpsr & CPSR_I)) {
        cpu_exception(g, 0x18, MODE_IRQ, c->r[15] + 4);
        g->now += bus_code_cycles(g, 0x18, 4, false) + bus_code_cycles(g, 0x1C, 4, true); /* pipeline refill */
        c->next_fetch_seq = true;
    }

    uint32_t pc = c->r[15];
    c->branched = false;
    g->bus.data_access = false;
    g->bus.prefetch_hint = false;
    g->bus.off_rom = 0;
    if (c->cpsr & CPSR_T) {
        uint16_t op = (uint16_t)bus_fetch16_inline(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 4;
        thumb_execute(g, op);
        if (!c->branched) c->r[15] = pc + 2;
    } else {
        uint32_t op = bus_fetch32_inline(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 8;
        if ((cpu_cond_table[op >> 28] >> (c->cpsr >> 28)) & 1) arm_execute(g, op);
        if (!c->branched) c->r[15] = pc + 4;
    }
    if (c->branched) { /* pipeline refill: N at the target, S after it; the next fetch is then sequential */
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_code_cycles(g, c->r[15], size, false) + bus_code_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
        g->bus.prefetch_end = 0; /* a branch empties the prefetch buffer */
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false; /* the bus was used for data (or the multiplier): the next fetch is non-sequential */
        if (g->bus.prefetch_hint) bus_prefetch_stall(g);
    }
}

void cpu_step(gba_t *g) {
    step(g);
}

/* The threaded loop runs what it can; cpu_step takes each instruction it hands back. */
void cpu_run_generic(gba_t *g) {
    while (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) {
        if (g->cpu.cpsr & CPSR_T) thumb_fast_run(g);
        else arm_fast_run(g);
        if (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) step(g);
    }
}

void cpu_run(gba_t *g) {
#ifdef GBA_ARM64
    a64_cpu_run(g);
#else
    cpu_run_generic(g);
#endif
}
