/* The serial port with nothing plugged in, and the joypad. */
#include "gb_internal.h"

/* One bit moves on each falling edge of the divider's bit 8 (8192 Hz) while a transfer with the internal clock
 * runs; with no cable, 1s come in. */
void gb_serial_clock(gb_t *g) {
    gb_serial_t *s = &g->serial;
    if (!s->bits_left) return;
    s->sb = (uint8_t)(s->sb << 1 | 1);
    if (--s->bits_left == 0) {
        s->sc &= 0x7F;
        g->mem.iflag |= 0x08;
    }
}

uint8_t gb_serial_read(const gb_t *g, uint16_t addr) {
    return addr == 0xFF01 ? g->serial.sb : g->serial.sc | 0x7E;
}

void gb_serial_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_serial_t *s = &g->serial;
    if (addr == 0xFF01) {
        s->sb = value;
        return;
    }
    s->sc = value & 0x81;
    s->bits_left = 0;
    if ((value & 0x81) == 0x81) { /* start, our clock; with the other side's clock nothing ever arrives */
        s->bits_left = 8;
        if (g->serial_len < sizeof g->serial_out) g->serial_out[g->serial_len++] = (char)s->sb;
    }
}

/* The four input lines: low (0) for a held key on a selected row. */
static uint8_t joypad_lines(const gb_joypad_t *j) {
    uint8_t lines = 0x0F;
    if (!(j->select & 0x10)) lines &= (uint8_t)~(j->keys >> 4); /* right left up down */
    if (!(j->select & 0x20)) lines &= (uint8_t)~j->keys;        /* A B Select Start */
    return lines & 0x0F;
}

uint8_t gb_joypad_read(const gb_t *g) {
    return 0xC0 | g->joypad.select | joypad_lines(&g->joypad);
}

/* A line going low asks for the joypad interrupt. */
static void joypad_set(gb_t *g, uint8_t select, uint8_t keys) {
    uint8_t before = joypad_lines(&g->joypad);
    g->joypad.select = select;
    g->joypad.keys = keys;
    if (before & ~joypad_lines(&g->joypad)) g->mem.iflag |= 0x10;
}

void gb_joypad_write(gb_t *g, uint8_t value) {
    joypad_set(g, value & 0x30, g->joypad.keys);
}

void gb_set_keys(gb_t *g, uint8_t pressed) {
    joypad_set(g, g->joypad.select, pressed);
}
