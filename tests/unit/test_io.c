#include "gba_internal.h"
#include "test.h"

TEST(io_dispstat_status_bits_are_read_only) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = 0x0003;
    bus_write16(g, 0x04000004, 0xFFFF, false);
    CHECK_EQ(g->ppu.dispstat, 0xFF3B);
    gba_destroy(g);
}

TEST(io_if_is_write_one_to_clear) {
    gba_t *g = gba_create(NULL);
    g->irq.if_ = 0x0005;
    bus_write16(g, 0x04000202, 0x0001, false);
    CHECK_EQ(g->irq.if_, 0x0004);
    g->irq.if_ = 0x0101;
    bus_write8(g, 0x04000203, 0x01, false);
    CHECK_EQ(g->irq.if_, 0x0001);
    gba_destroy(g);
}

TEST(io_ie_ime_waitcnt_round_trip) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x04000200, 0xFFFF, false);
    CHECK_EQ(bus_read16(g, 0x04000200, false), 0x3FFF);
    bus_write16(g, 0x04000208, 0xFFFF, false);
    CHECK_EQ(bus_read16(g, 0x04000208, false), 1);
    bus_write16(g, 0x04000204, 0x4317, false);
    CHECK_EQ(bus_read16(g, 0x04000204, false), 0x4317);
    CHECK_EQ(g->bus.waitcnt, 0x4317);
    gba_destroy(g);
}

TEST(io_32bit_write_splits_into_halves) {
    gba_t *g = gba_create(NULL);
    g->irq.if_ = 0x0007;
    bus_write32(g, 0x04000200, 0x00050003, false);
    CHECK_EQ(g->irq.ie, 0x0003);
    CHECK_EQ(g->irq.if_, 0x0002);
    gba_destroy(g);
}

TEST(io_keyinput_reflects_pressed_keys) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read16(g, 0x04000130, false), 0x03FF);
    gba_set_keys(g, GBA_KEY_A | GBA_KEY_START);
    CHECK_EQ(bus_read16(g, 0x04000130, false), 0x03F6);
    gba_destroy(g);
}

TEST(io_haltcnt_write_halts_cpu) {
    gba_t *g = gba_create(NULL);
    bus_write8(g, 0x04000301, 0, false);
    CHECK(g->irq.halted);
    gba_destroy(g);
}

TEST(io_unhandled_registers_are_stored) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x0400000C, 0x1234, false);
    CHECK_EQ(bus_read16(g, 0x0400000C, false), 0x1234);
    bus_write8(g, 0x0400000D, 0xAB, false);
    CHECK_EQ(bus_read16(g, 0x0400000C, false), 0xAB34);
    gba_destroy(g);
}
