#include "cart/hardware.h"

#include <string.h>

#include "gba_internal.h"

#define CYCLES_PER_SECOND 16777216u
#define CLOCK_DEFAULT 946684800 /* 2000-01-01 00:00:00 */

/* Games with extras, by the first three letters of the game code (every region), after mGBA's list (where each
 * code's game is named). */
static const struct {
    char code[4];
    uint32_t devices;
    gba_save_type_t save;
} games[] = {
    {"AXV", GBA_HW_RTC, GBA_SAVE_FLASH128},
    {"AXP", GBA_HW_RTC, GBA_SAVE_FLASH128},
    {"BPE", GBA_HW_RTC, GBA_SAVE_FLASH128},
    {"U3I", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},
    {"U32", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},
    {"U33", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},
    {"BR4", GBA_HW_RTC, GBA_SAVE_FLASH64},
    {"BKA", GBA_HW_RTC, GBA_SAVE_FLASH128},
    {"BLJ", GBA_HW_RTC, GBA_SAVE_FLASH64},
    {"BLV", GBA_HW_RTC, GBA_SAVE_FLASH64},
    {"RZW", GBA_HW_GYRO | GBA_HW_RUMBLE, GBA_SAVE_SRAM},
    {"V49", GBA_HW_RUMBLE, GBA_SAVE_SRAM},
    {"2GB", GBA_HW_RUMBLE, GBA_SAVE_SRAM},
    {"KYG", GBA_HW_TILT, GBA_SAVE_EEPROM},
    {"KHP", GBA_HW_TILT, GBA_SAVE_EEPROM},
};

/* The RTC library leaves its name in the ROM (word aligned), even in hacks with a changed code. */
static bool has_rtc_marker(const cart_t *c) {
    for (uint32_t off = 0; off + 8 <= c->rom_size; off += 4) {
        if (c->rom[off] == 'S' && memcmp(c->rom + off, "SIIRTC_V", 8) == 0) return true;
    }
    return false;
}

/* Everything but the clock base and the app's inputs back to power-on. */
static void reset_devices(hardware_t *h) {
    h->data = h->direction = h->readable = h->pins = 0;
    memset(&h->rtc, 0, sizeof h->rtc);
    h->rtc.control = 0x40; /* 24-hour mode, no "power lost" flag */
    h->rtc.sck = true;
    h->rtc.sio = 1;
    h->solar_counter = 0;
    h->solar_target = 0xE8;
    h->solar_clock = false;
    h->gyro_shift = 0;
    h->gyro_out = 0;
    h->gyro_clock = false;
    h->tilt_state = 0;
    h->tilt_x = 0x392;
    h->tilt_y = 0x3A0;
}

void hardware_setup(gba_t *g) {
    cart_t *c = &g->cart;
    hardware_t *h = &c->hw;
    memset(h, 0, sizeof *h);
    for (size_t k = 0; k < sizeof games / sizeof games[0]; k++) {
        if (memcmp(c->code, games[k].code, 3) == 0) {
            h->devices = games[k].devices;
            gba_set_save_type(g, games[k].save);
            break;
        }
    }
    if (!h->devices && has_rtc_marker(c)) h->devices = GBA_HW_RTC;
    reset_devices(h);
    h->clock_base = CLOCK_DEFAULT;
    h->clock_cycle = g->now;
}

int64_t hardware_local_seconds(const gba_t *g) {
    const hardware_t *h = &g->cart.hw;
    uint64_t elapsed = g->now > h->clock_cycle ? g->now - h->clock_cycle : 0;
    return h->clock_base + (int64_t)(elapsed / CYCLES_PER_SECOND);
}

void hardware_reset(gba_t *g) {
    hardware_t *h = &g->cart.hw;
    h->clock_base = hardware_local_seconds(g); /* the clock keeps running across a reset */
    h->clock_cycle = 0;                        /* gba_reset restarts the cycle count next */
    h->data = h->direction = h->readable = h->pins = 0;
    h->rtc.active = false;
    h->rtc.sck = true;
    h->rtc.bits = h->rtc.bit_pos = h->rtc.byte_pos = 0;
    h->rtc.sio = 1;
    h->solar_counter = 0;
    h->gyro_shift = 0;
    h->gyro_out = 0;
    h->tilt_state = 0;
}

/* ---- tilt sensor (0x0E008000-0x0E0085FF) ---- */

bool tilt_mapped(const gba_t *g, uint32_t addr) {
    uint32_t off = addr & 0xFFFF;
    return (g->cart.hw.devices & GBA_HW_TILT) && off >= 0x8000 && off < 0x8600;
}

static uint16_t tilt_value(uint16_t centre, int16_t input) {
    return (uint16_t)(centre + input * 0xE0 / 32768);
}

uint8_t tilt_read(const hardware_t *h, uint32_t addr) {
    switch (addr & 0xFFFF) {
    case 0x8200: return (uint8_t)h->tilt_x;
    case 0x8300: return (uint8_t)(((h->tilt_x >> 8) & 0x0F) | 0x80); /* bit 7: reading ready */
    case 0x8400: return (uint8_t)h->tilt_y;
    case 0x8500: return (uint8_t)((h->tilt_y >> 8) & 0x0F);
    default: return 0xFF;
    }
}

void tilt_write(hardware_t *h, uint32_t addr, uint8_t value) {
    uint32_t off = addr & 0xFFFF;
    if (off == 0x8000 && value == 0x55) {
        h->tilt_state = 1;
    } else if (off == 0x8100 && value == 0xAA && h->tilt_state == 1) { /* 0x55 then 0xAA: take a reading */
        h->tilt_state = 0;
        h->tilt_x = tilt_value(0x392, h->tilt_in_x);
        h->tilt_y = tilt_value(0x3A0, h->tilt_in_y);
    }
}

/* ---- public API ---- */

uint32_t gba_cart_hardware(const gba_t *g) {
    return g->cart.hw.devices;
}

void gba_set_cart_hardware(gba_t *g, uint32_t flags) {
    g->cart.hw.devices = flags & 0x1F;
    reset_devices(&g->cart.hw);
}

void gba_set_rtc(gba_t *g, int64_t unix_seconds, int32_t utc_offset_s) {
    g->cart.hw.clock_base = unix_seconds + utc_offset_s;
    g->cart.hw.clock_cycle = g->now;
}

void gba_set_light(gba_t *g, uint8_t level) {
    g->cart.hw.light = level;
}

void gba_set_gyro(gba_t *g, int16_t z_rate) {
    g->cart.hw.gyro_rate = z_rate;
}

void gba_set_tilt(gba_t *g, int16_t x, int16_t y) {
    g->cart.hw.tilt_in_x = x;
    g->cart.hw.tilt_in_y = y;
}

int gba_rumble_active(const gba_t *g) {
    const hardware_t *h = &g->cart.hw;
    return (h->devices & GBA_HW_RUMBLE) && (h->direction & h->data & 8);
}
