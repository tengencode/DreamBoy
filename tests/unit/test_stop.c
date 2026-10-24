#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static gba_t *spinning(void) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    return tu_arm(ops, TU_LEN(ops));
}

static void drain(gba_t *g) {
    int16_t buf[4096];
    while (gba_audio_read(g, buf, 2048) > 0) {}
}

TEST(stop_freezes_the_console_and_hands_out_silence) {
    gba_t *g = spinning();
    gba_run_frame(g);
    drain(g);
    bus_write8(g, 0x04000301, 0x80, false); /* HALTCNT bit 7: Stop */
    CHECK(g->irq.stopped);
    uint64_t now = g->now;
    uint16_t vcount = g->ppu.vcount;
    gba_run_frame(g);
    CHECK_EQ(g->now, now);
    CHECK_EQ(g->ppu.vcount, vcount);
    int16_t buf[2 * 1024];
    size_t n = gba_audio_read(g, buf, 1024);
    CHECK_EQ(n, 548); /* 280,896 / 512 = 548 rest 320 */
    for (size_t i = 0; i < n * 2; i++) CHECK_EQ(buf[i], 0);
    gba_run_frame(g);
    CHECK_EQ(gba_audio_read(g, buf, 1024), 549); /* the rest carries over */
    gba_destroy(g);
}

TEST(stop_wakes_on_an_enabled_keypad_irq) {
    gba_t *g = spinning();
    g->irq.ie = IRQ_KEYPAD;
    tu_io16(g, 0x132, 0x4000 | GBA_KEY_A);
    bus_write8(g, 0x04000301, 0x80, false);
    gba_run_frame(g);
    CHECK(g->irq.stopped);
    uint64_t now = g->now;
    gba_set_keys(g, GBA_KEY_A);
    gba_run_frame(g);
    CHECK(!g->irq.stopped);
    CHECK(g->now > now);
    gba_destroy(g);
}

TEST(stop_ignores_other_interrupts) {
    gba_t *g = spinning();
    g->irq.ie = IRQ_VBLANK | IRQ_KEYPAD;
    bus_write8(g, 0x04000301, 0x80, false);
    irq_raise(g, IRQ_VBLANK);
    gba_run_frame(g);
    CHECK(g->irq.stopped);
    gba_destroy(g);
}

TEST(stop_from_the_bios_call_and_halt_still_halts) {
    gba_t *g = spinning();
    hle_swi(g, 0x03);
    CHECK(g->irq.stopped);
    gba_destroy(g);
    g = spinning();
    bus_write8(g, 0x04000301, 0x00, false);
    CHECK(g->irq.halted);
    CHECK(!g->irq.stopped);
    gba_destroy(g);
}
