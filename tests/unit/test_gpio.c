#include "test.h"
#include "testutil.h"

static void port(gba_t *g, uint32_t reg, uint16_t value) {
    bus_write16(g, 0x08000000 + reg, value, false);
}

static uint16_t pins(gba_t *g) {
    return (uint16_t)bus_read16(g, 0x080000C4, false);
}

TEST(gpio_registers_readable_only_when_enabled) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    CHECK_EQ(bus_read16(g, 0x080000C6, false), 0xEEEE); /* write-only: the ROM's bytes */
    port(g, 0xC6, 0x0005);
    port(g, 0xC8, 0x0001);
    CHECK_EQ(bus_read16(g, 0x080000C6, false), 0x0005);
    CHECK_EQ(bus_read16(g, 0x080000C8, false), 0x0001);
    CHECK_EQ(bus_read32(g, 0x080000C4, false) >> 16, 0x0005);
    CHECK_EQ(bus_read16(g, 0x0A0000C6, false), 0x0005); /* every ROM mirror */
    port(g, 0xC8, 0x0000);
    CHECK_EQ(bus_read16(g, 0x080000C6, false), 0xEEEE);
    gba_destroy(g);
}

TEST(gpio_ignores_byte_writes) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    bus_write8(g, 0x080000C8, 1, false);
    CHECK_EQ(bus_read16(g, 0x080000C8, false), 0xEEEE);
    gba_destroy(g);
}

TEST(gpio_absent_without_port_devices) {
    gba_t *g = tu_rom_code("KYGE", NULL); /* tilt only: no GPIO port */
    port(g, 0xC8, 0x0001);
    CHECK_EQ(bus_read16(g, 0x080000C8, false), 0xEEEE);
    gba_destroy(g);
}

/* GBATEK's solar routine: reset the counter, then clock it until pin 3 answers. */
static int solar_clocks(gba_t *g) {
    port(g, 0xC8, 1);
    port(g, 0xC6, 7); /* pins 0-2 out, pin 3 in */
    port(g, 0xC4, 2); /* reset */
    port(g, 0xC4, 0);
    for (int n = 1; n <= 0x100; n++) {
        port(g, 0xC4, 1);
        port(g, 0xC4, 0);
        if (pins(g) & 8) return n;
    }
    return -1;
}

TEST(gpio_solar_sensor_levels) {
    gba_t *g = tu_rom_code("U3IE", NULL);
    CHECK_EQ(solar_clocks(g), 0xE8); /* dark */
    gba_set_light(g, 255);
    CHECK_EQ(solar_clocks(g), 0x50); /* full sun */
    gba_set_light(g, 128);
    CHECK_EQ(solar_clocks(g), 0x9C);
    gba_destroy(g);
}

/* GBATEK's gyro routine: start a conversion, then clock out 16 bits (4 zeros + 12 data). */
static uint16_t gyro_value(gba_t *g) {
    port(g, 0xC8, 1);
    port(g, 0xC6, 0x0B); /* pin 2 in, the rest out */
    port(g, 0xC4, 3);    /* start, clock high */
    port(g, 0xC4, 2);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
        uint16_t bit = (pins(g) >> 2) & 1;
        port(g, 0xC4, 0); /* clock low: the next bit */
        port(g, 0xC4, 2);
        v = (uint16_t)(v << 1 | bit);
    }
    return v;
}

TEST(gpio_gyro_reports_rotation) {
    gba_t *g = tu_rom_code("RZWE", NULL);
    CHECK_EQ(gyro_value(g), 0x06C0); /* still */
    gba_set_gyro(g, 16384);
    CHECK_EQ(gyro_value(g), 0x0880); /* clockwise */
    gba_set_gyro(g, -16384);
    CHECK_EQ(gyro_value(g), 0x0500);
    gba_set_gyro(g, 32767);
    CHECK_EQ(gyro_value(g), 0x0A3F);
    gba_destroy(g);
}

TEST(gpio_rumble_follows_pin_3) {
    gba_t *g = tu_rom_code("V49E", NULL);
    CHECK_EQ(gba_rumble_active(g), 0);
    port(g, 0xC6, 8);
    port(g, 0xC4, 8);
    CHECK_EQ(gba_rumble_active(g), 1);
    port(g, 0xC4, 0);
    CHECK_EQ(gba_rumble_active(g), 0);
    port(g, 0xC4, 8);
    port(g, 0xC6, 0); /* pin 3 as an input: off */
    CHECK_EQ(gba_rumble_active(g), 0);
    gba_destroy(g);
}
