#include "bios/hle.h"
#include "bios/hle_internal.h"
#include "test.h"
#include "testutil.h"

/* Cycles taken by the next n instructions. With WAITCNT = 0 the cartridge (WS0) costs 5 cycles for a
 * first (N) 16-bit read and 3 for a next (S) one; a 32-bit read is two of them (N+S = 8, S+S = 6).
 * IWRAM costs 1 cycle for everything. */
static uint64_t cycles_of(gba_t *g, int n) {
    uint64_t start = g->now;
    tu_steps(g, n);
    return g->now - start;
}

TEST(cycles_arm_rom_straight_line_is_sequential) {
    const uint32_t ops[] = {0xE1A00000, 0xE1A00000, 0xE1A00000}; /* nop x3 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);               /* the first fetch after boot */
    CHECK_EQ(cycles_of(g, 1), 6); /* S (32-bit): 3 + 3 */
    CHECK_EQ(cycles_of(g, 1), 6);
    gba_destroy(g);
}

TEST(cycles_arm_load_makes_the_next_fetch_nonsequential) {
    const uint32_t ops[] = {0xE1A00000, 0xE5910000, 0xE1A00000, 0xE1A00000}; /* nop; ldr r0, [r1]; nop; nop */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x03000000; /* IWRAM data: 1 cycle */
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 1 + 1); /* S fetch + N data + I */
    CHECK_EQ(cycles_of(g, 1), 8);         /* N fetch: 5 + 3 */
    CHECK_EQ(cycles_of(g, 1), 6);
    gba_destroy(g);
}

TEST(cycles_arm_multiply_makes_the_next_fetch_nonsequential) {
    const uint32_t ops[] = {0xE1A00000, 0xE0000190, 0xE1A00000}; /* nop; mul r0, r0, r1; nop */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0xFF; /* multiplier: 1 internal cycle */
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 1);
    CHECK_EQ(cycles_of(g, 1), 8);
    gba_destroy(g);
}

TEST(cycles_arm_branch_refills_the_pipeline) {
    const uint32_t ops[] = {0xE1A00000, 0xEA000000, 0xE1A00000, 0xE1A00000, 0xE1A00000}; /* nop; b +8 (to [3]); ... */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 8 + 6); /* S (the branch) + N and S at the target */
    CHECK_EQ(g->cpu.r[15], 0x0800000C);
    CHECK_EQ(cycles_of(g, 1), 6);         /* the target's own fetch is sequential */
    gba_destroy(g);
}

TEST(cycles_thumb_branch_refills_the_pipeline) {
    const uint16_t ops[] = {0x46C0, 0xE000, 0x46C0, 0x46C0}; /* nop; b +4 (to [3]); nop; nop */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 3 + 5 + 3);
    CHECK_EQ(g->cpu.r[15], 0x08000006);
    CHECK_EQ(cycles_of(g, 1), 3);
    gba_destroy(g);
}

TEST(cycles_ldm_crossing_into_the_cartridge_is_nonsequential) {
    const uint32_t prog[] = {0xE1A00000, 0xE8B10018, 0xE1A00000}; /* nop; ldmia r1!, {r3, r4}; nop */
    gba_t *g = tu_arm(prog, TU_LEN(prog));
    g->cpu.r[1] = 0x07FFFFFC; /* last OAM word, then the first cartridge word */
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 1 + 8 + 1); /* S fetch + OAM N + ROM N (not S) + I */
    gba_destroy(g);
}

TEST(cycles_irq_entry_refills_the_pipeline) {
    const uint32_t ops[] = {0xE1A00000, 0xE1A00000};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    g->irq.ie = IRQ_VBLANK;
    g->irq.ime = 1;
    irq_raise(g, IRQ_VBLANK);
    tu_advance_to(g, g->now + 16); /* past any IRQ delay */
    uint64_t start = g->now;
    tu_steps(g, 1); /* IRQ entry, then the BIOS instruction at 0x18 (b irq_vector) */
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_IRQ);
    CHECK_EQ(g->now - start, 2 + 1 + 2); /* refill (N+S in the BIOS) + the vector's own fetch + its branch's refill */
    gba_destroy(g);
}

/* ---- timers ---- */

TEST(timer_starts_counting_two_cycles_after_the_write) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x100, 0x0000);
    tu_io16(g, 0x102, 0x0080); /* start, prescaler 1 */
    uint64_t t = g->now;
    g->now = t + 2;
    CHECK_EQ(timer_read_counter(g, 0), 0);
    g->now = t + 3;
    CHECK_EQ(timer_read_counter(g, 0), 1);
    gba_destroy(g);
}

TEST(prescaled_timer_starts_on_the_prescaler_grid) {
    gba_t *g = gba_create(NULL);
    g->now = 100 * 64 + 10; /* between two ticks of the /64 prescaler */
    tu_io16(g, 0x100, 0x0000);
    tu_io16(g, 0x102, 0x0081); /* start, prescaler 64 */
    g->now = 101 * 64 - 1;
    CHECK_EQ(timer_read_counter(g, 0), 0);
    g->now = 101 * 64; /* the first tick is on the shared grid, not 64 cycles after the write */
    CHECK_EQ(timer_read_counter(g, 0), 1);
    gba_destroy(g);
}

/* ---- BIOS routine times ---- */

TEST(bios_routine_times_follow_mgbas_formulas) {
    CHECK_EQ(hle_div_cycles(0x12345678, 0xFF), 284);
    CHECK_EQ(hle_div_cycles(0xFF, 0x12345678), 24);
    CHECK_EQ(hle_sqrt_cycles(0), 53);
    CHECK_EQ(hle_sqrt_cycles(0xFF), 168);
    CHECK_EQ(hle_sqrt_cycles(0x12345678), 1084);
    CHECK_EQ(hle_arctan_cycles(0xFF), 53);
    CHECK_EQ(hle_arctan2_cycles(5, 0), 11);
}

TEST(bios_division_charges_its_routine_time) {
    gba_t *g = gba_create(NULL);
    g->cpu.r[0] = 0x12345678;
    g->cpu.r[1] = 0xFF;
    uint64_t t = g->now;
    hle_swi(g, 0x06);
    CHECK_EQ(g->now - t, 284 - 8);
    gba_destroy(g);
}

/* ---- DMA ---- */

TEST(dma_immediate_takes_the_bus_three_cycles_after_the_write) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x03000000, 0xCAFEBABE, false);
    tu_io32(g, 0x0D4, 0x03000000);
    tu_io32(g, 0x0D8, 0x03000100);
    tu_io16(g, 0x0DC, 1);
    tu_io16(g, 0x0DE, 0x8400); /* enable, 32-bit, immediate */
    uint64_t t = g->now;
    CHECK_EQ(bus_peek(g, 0x03000100, 4), 0); /* the CPU may still run for 3 cycles */
    g->now = t + 3;
    CHECK_EQ(bus_read32(g, 0x03000100, false), 0xCAFEBABE); /* this access waited for the DMA */
    CHECK_EQ(g->now, t + 3 + 1 + 1 + 2 + 1); /* DMA read + write + 2, then the CPU's own read */
    gba_destroy(g);
}

TEST(dma_from_the_cartridge_always_increments) {
    const uint32_t words[] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    gba_t *g = tu_arm(words, TU_LEN(words));
    tu_io32(g, 0x0D4, 0x08000000);
    tu_io32(g, 0x0D8, 0x03000000);
    tu_io16(g, 0x0DC, 4);
    tu_io16(g, 0x0DE, 0x8500); /* enable, 32-bit, immediate, source fixed (ignored for ROM) */
    tu_advance_to(g, g->now + 3);
    CHECK_EQ(bus_peek(g, 0x0300000C, 4), 0x44444444);
    gba_destroy(g);
}

/* ---- prefetch buffer ---- */

/* nop; ldr r0, [r1] (IWRAM); nop; nop from the cartridge, with WAITCNT bit 14 on or off. */
static void prefetch_run(uint16_t waitcnt, uint64_t out[3]) {
    const uint32_t ops[] = {0xE1A00000, 0xE5910000, 0xE1A00000, 0xE1A00000};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_io16(g, 0x204, waitcnt);
    g->cpu.r[1] = 0x03000000;
    tu_steps(g, 1);
    for (int i = 0; i < 3; i++) out[i] = cycles_of(g, 1);
    gba_destroy(g);
}

TEST(prefetch_buffer_hides_cartridge_waits_after_a_ram_load) {
    uint64_t off[3], on[3];
    prefetch_run(0x0000, off);
    prefetch_run(0x4000, on);
    CHECK_EQ(off[0], 8);
    CHECK_EQ(off[1], 8);
    CHECK_EQ(off[2], 6);
    CHECK_EQ(on[0], 6); /* the load's off-cartridge time lets the buffer fetch ahead: 2 cycles credited */
    CHECK_EQ(on[1], 6); /* the next fetch comes from the buffer: sequential */
    CHECK_EQ(on[2], 6);
}

/* ---- interrupt delay ---- */

TEST(irq_reaches_the_cpu_five_cycles_after_the_request) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_VBLANK;
    g->irq.ime = 1;
    irq_changed(g);
    uint64_t t = g->now;
    irq_raise(g, IRQ_VBLANK);
    g->now = t + 4;
    CHECK(!irq_line(g));
    g->now = t + 5;
    CHECK(irq_line(g));
    gba_destroy(g);
}

TEST(halted_cpu_wakes_three_cycles_after_a_running_one_would_see_the_request) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_VBLANK;
    irq_changed(g);
    uint64_t t = g->now;
    irq_raise(g, IRQ_VBLANK);
    CHECK_EQ(irq_wake_at(g), t + 5 + 3);
    g->now = t + 7;
    CHECK(!irq_wake(g));
    g->now = t + 8;
    CHECK(irq_wake(g));
    gba_destroy(g);
}

TEST(irq_from_an_event_counts_from_the_event_time) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_TIMER0;
    g->irq.ime = 1;
    irq_changed(g);
    irq_raise_at(g, IRQ_TIMER0, 1000);
    CHECK_EQ(g->irq.visible_at, 1005);
    gba_destroy(g);
}

TEST(halted_cpu_wakes_when_the_request_arrives) {
    const uint32_t ops[] = {0xE1A00000, 0xE1A00000, 0xEAFFFFFE};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.ie = IRQ_VBLANK;
    irq_changed(g);
    irq_halt(g);
    irq_raise(g, IRQ_VBLANK); /* IME off: wakes, no interrupt */
    uint64_t t = g->now;
    gba_run_frame(g);
    CHECK(!g->irq.halted);
    CHECK(irq_wake_at(g) == t + 8);
    gba_destroy(g);
}

/* ---- H-blank ---- */

TEST(hblank_flag_at_1008_and_its_irq_seven_cycles_earlier) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = DISPSTAT_HBLANK_IRQ;
    g->irq.ie = IRQ_HBLANK;
    g->irq.ime = 1;
    irq_changed(g);
    tu_advance_to(g, 1007);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    tu_advance_to(g, 1008);
    CHECK(g->ppu.dispstat & DISPSTAT_HBLANK);
    CHECK(g->irq.if_ & IRQ_HBLANK);
    CHECK_EQ(g->irq.visible_at, 1001 + 5);
    gba_destroy(g);
}
