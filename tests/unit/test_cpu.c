#include "test.h"
#include "testutil.h"

static const uint32_t zero_rom[] = {0};

TEST(cpu_boot_state_skips_the_bios) {
    gba_t *g = tu_arm(zero_rom, TU_LEN(zero_rom));
    arm_cpu_t *c = &g->cpu;
    CHECK_EQ(c->r[15], 0x08000000);
    CHECK_EQ(c->cpsr, MODE_SYS);
    CHECK_EQ(c->r[13], 0x03007F00);
    CHECK_EQ(c->bank_r13[BANK_IRQ], 0x03007FA0);
    CHECK_EQ(c->bank_r13[BANK_SVC], 0x03007FE0);
    CHECK_EQ(g->io.postflg, 1);
    gba_destroy(g);
}

TEST(cpu_mode_switch_banks_registers) {
    gba_t *g = tu_arm(zero_rom, TU_LEN(zero_rom));
    arm_cpu_t *c = &g->cpu;
    c->r[8] = 8;
    c->r[13] = 13;
    c->r[14] = 14;
    cpu_write_cpsr(c, MODE_FIQ);
    CHECK_EQ(c->r[8], 0);
    CHECK_EQ(c->r[13], 0);
    c->r[8] = 88;
    cpu_write_cpsr(c, MODE_IRQ);
    CHECK_EQ(c->r[8], 8);
    CHECK_EQ(c->r[13], 0x03007FA0);
    cpu_write_cpsr(c, MODE_SYS);
    CHECK_EQ(c->r[13], 13);
    CHECK_EQ(c->r[14], 14);
    cpu_write_cpsr(c, MODE_FIQ);
    CHECK_EQ(c->r[8], 88);
    gba_destroy(g);
}

TEST(cpu_condition_codes) {
    CHECK(cpu_cond_passed(CPSR_Z, 0x0));
    CHECK(!cpu_cond_passed(0, 0x0));
    CHECK(cpu_cond_passed(CPSR_C, 0x8));
    CHECK(!cpu_cond_passed(CPSR_C | CPSR_Z, 0x8));
    CHECK(cpu_cond_passed(CPSR_N | CPSR_V, 0xA));
    CHECK(!cpu_cond_passed(CPSR_N, 0xA));
    CHECK(cpu_cond_passed(0, 0xC));
    CHECK(!cpu_cond_passed(CPSR_Z, 0xC));
    CHECK(cpu_cond_passed(0, 0xE));
    CHECK(!cpu_cond_passed(0xF0000000, 0xF));
}

TEST(cpu_undefined_instruction_enters_und_mode) {
    const uint32_t ops[] = {0xE7F000F1}; /* permanently undefined */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 1);
    CHECK_EQ(c->cpsr & CPSR_MODE, MODE_UND);
    CHECK(c->cpsr & CPSR_I);
    CHECK_EQ(c->r[14], 0x08000004);
    CHECK_EQ(c->spsr[BANK_UND], MODE_SYS);
    CHECK_EQ(c->r[15], 0x00000004);
    gba_destroy(g);
}

TEST(cpu_failed_condition_skips_instruction) {
    const uint32_t ops[] = {0x07F000F1}; /* undefined, but only if EQ; Z is clear */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000004);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    gba_destroy(g);
}

TEST(cpu_thumb_undefined_instruction) {
    const uint16_t ops[] = {0xDE00};
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 1);
    CHECK_EQ(c->cpsr & CPSR_MODE, MODE_UND);
    CHECK(!(c->cpsr & CPSR_T));
    CHECK_EQ(c->r[14], 0x08000002);
    CHECK_EQ(c->spsr[BANK_UND], MODE_SYS | CPSR_T);
    CHECK_EQ(c->r[15], 0x00000004);
    gba_destroy(g);
}

TEST(cpu_takes_enabled_irq_before_next_instruction) {
    const uint32_t ops[] = {0x07F000F1};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    g->irq.ie = IRQ_VBLANK;
    g->irq.if_ = IRQ_VBLANK;
    g->irq.ime = 1;
    irq_changed(g);
    g->now += 5;    /* the request reaches the CPU 5 cycles later */
    tu_steps(g, 1); /* enters IRQ mode, then executes the instruction at the IRQ vector */
    CHECK_EQ(c->cpsr & CPSR_MODE, MODE_IRQ);
    CHECK(c->cpsr & CPSR_I);
    CHECK_EQ(c->r[14], 0x08000004);
    CHECK_EQ(c->spsr[BANK_IRQ], MODE_SYS);
    gba_destroy(g);
}

TEST(cpu_ignores_irq_when_ime_clear_or_cpsr_i_set) {
    const uint32_t ops[] = {0x07F000F1, 0x07F000F1};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.ie = IRQ_VBLANK;
    g->irq.if_ = IRQ_VBLANK;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    g->irq.ime = 1;
    g->cpu.cpsr |= CPSR_I;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    CHECK_EQ(g->cpu.r[15], 0x08000008);
    gba_destroy(g);
}
