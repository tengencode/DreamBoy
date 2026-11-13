/* The SM83 CPU's interrupts, EI / DI, HALT and its bug. */
#include "gbtestutil.h"
#include "test.h"

static gb_t *boot(const uint8_t *code, size_t n) {
    gb_t *g = gbtu_boot(code, n);
    gb_debug_step(g);
    gb_debug_step(g);
    return g;
}

TEST(gb_cpu_irq_dispatch) {
    static const uint8_t code[] = {0x00, 0x00};
    gb_t *g = boot(code, 2);
    g->cpu.ime = 1;
    g->mem.ie = 0x1F;
    g->mem.iflag = 0x14; /* timer and joypad: the timer (bit 2) goes first */
    uint64_t before = gb_debug_ticks(g);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_ticks(g) - before, 20);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0050);
    CHECK_EQ(g->mem.iflag, 0x10);
    CHECK_EQ(g->cpu.ime, 0);
    CHECK_EQ(gb_debug_reg(g, GB_REG_SP), 0xFFFC);
    CHECK_EQ(gb_debug_peek(g, 0xFFFC), 0x50);
    CHECK_EQ(gb_debug_peek(g, 0xFFFD), 0x01);
    gb_destroy(g);
    for (int bit = 0; bit < 5; bit++) {
        g = boot(code, 2);
        g->cpu.ime = 1;
        g->mem.ie = g->mem.iflag = (uint8_t)(1 << bit);
        gb_debug_step(g);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x40 + 8 * bit);
        gb_destroy(g);
    }
    g = boot(code, 2); /* not enabled, or interrupts off: nothing */
    g->cpu.ime = 1;
    g->mem.ie = 0x01;
    g->mem.iflag = 0x02;
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x151);
    g->cpu.ime = 0;
    g->mem.iflag = 0x01;
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x152);
    gb_destroy(g);
}

TEST(gb_cpu_if_ie_registers) {
    static const uint8_t code[] = {0x3E, 0xFF, 0xE0, 0x0F, 0xE0, 0xFF}; /* LD A,0xFF; LDH (0F),A; LDH (FF),A */
    gb_t *g = boot(code, sizeof code);
    for (int i = 0; i < 3; i++) gb_debug_step(g);
    CHECK_EQ(gb_debug_peek(g, 0xFF0F), 0xFF);
    CHECK_EQ(g->mem.iflag, 0x1F);
    CHECK_EQ(gb_debug_peek(g, 0xFFFF), 0xFF);
    g->mem.iflag = 0;
    CHECK_EQ(gb_debug_peek(g, 0xFF0F), 0xE0);
    gb_destroy(g);
}

TEST(gb_cpu_ei_di_reti) {
    static const uint8_t code[] = {0xFB, 0x00, 0x00}; /* EI; NOP; NOP */
    gb_t *g = boot(code, 3);
    g->mem.ie = g->mem.iflag = 0x01;
    gb_debug_step(g); /* EI */
    gb_debug_step(g); /* the instruction after EI still runs */
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x152);
    gb_debug_step(g); /* now the interrupt */
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0040);
    CHECK_EQ(gb_debug_peek(g, 0xFFFC), 0x52);
    gb_destroy(g);

    static const uint8_t ei_di[] = {0xFB, 0xF3, 0x00, 0x00}; /* EI; DI: no interrupt */
    g = boot(ei_di, 4);
    g->mem.ie = g->mem.iflag = 0x01;
    for (int i = 0; i < 4; i++) gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x154);
    CHECK_EQ(g->cpu.ime, 0);
    gb_destroy(g);

    static const uint8_t reti[] = {0x31, 0x00, 0xD0, 0x01, 0x60, 0x01, 0xC5, 0xD9}; /* SP; BC=0x160; PUSH; RETI */
    g = boot(reti, sizeof reti);
    for (int i = 0; i < 4; i++) gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x160);
    CHECK_EQ(g->cpu.ime, 1);
    gb_destroy(g);
}

TEST(gb_cpu_halt) {
    static const uint8_t code[] = {0x76, 0x3C, 0x00}; /* HALT; INC A */
    gb_t *g = boot(code, 3);
    g->cpu.a = 0;
    g->mem.ie = 0x04;
    gb_debug_step(g);
    for (int i = 0; i < 5; i++) gb_debug_step(g); /* idle: PC stays, time passes */
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x151);
    CHECK_EQ(g->cpu.a, 0);
    g->mem.iflag = 0x04; /* interrupts off: the CPU wakes and carries on without jumping */
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(g->cpu.a, 1);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x152);
    CHECK_EQ(g->mem.iflag, 0x04);
    gb_destroy(g);

    g = boot(code, 3); /* interrupts on: the handler runs, returning to after the HALT */
    g->cpu.ime = 1;
    g->mem.ie = 0x04;
    gb_debug_step(g);
    gb_debug_step(g);
    g->mem.iflag = 0x04;
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0050);
    CHECK_EQ(gb_debug_peek(g, 0xFFFC), 0x51);
    gb_destroy(g);
}

TEST(gb_cpu_halt_bug) {
    static const uint8_t code[] = {0x76, 0x3C, 0x00}; /* HALT; INC A — with an interrupt waiting and IME off */
    gb_t *g = boot(code, 3);
    g->cpu.a = 0;
    g->mem.ie = g->mem.iflag = 0x04;
    gb_debug_step(g); /* HALT does not stop */
    gb_debug_step(g); /* INC A, PC not moved */
    gb_debug_step(g); /* INC A again */
    CHECK_EQ(g->cpu.a, 2);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x152);
    gb_destroy(g);
}

/* The vector is picked after the high byte of PC is pushed: a push that lands on IE can cancel the interrupt. */
TEST(gb_cpu_irq_push_onto_ie) {
    static const uint8_t code[] = {0x00, 0x00};
    gb_t *g = boot(code, 2);
    g->cpu.sp = 0x0000; /* the high byte (0x01) goes to 0xFFFF = IE */
    g->cpu.ime = 1;
    g->mem.ie = 0x04;
    g->mem.iflag = 0x04;
    gb_debug_step(g);
    CHECK_EQ(g->mem.ie, 0x01);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0000);
    CHECK_EQ(g->mem.iflag, 0x04);
    gb_destroy(g);
}
